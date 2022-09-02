/*
 * cellulard.c
 *
 * Copyright (C) 2022 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <gio/gio.h>
#include <osso-log.h>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>

#include <gofono_manager.h>
#include <gofono_modem.h>

#include <errno.h>
#include <getopt.h>

static gboolean normal_mode;
static GMainLoop *loop = NULL;

static OfonoManager *manager = NULL;
static gulong modem_removed_id;
static gulong modem_added_id;
static gulong manager_valid_id;
static GHashTable *modems;
static GHashTable *ids;

static void
control_modem(OfonoModem* modem);

static void
online_modem(OfonoModem* modem, gboolean online)
{
  /* don't try to offline powered-off modem */
  if (online && modem->online != online)
  {
    DLOG_INFO("modem %s set to online", ofono_modem_path(modem));
    /*
     * FIXME - ugly hack to make ofono on d4 happy, revert once ofono bug is
     * fixed
     */
    sleep(3);
    ofono_modem_set_online(modem, online);
  }
}

static void
modem_disposed_cb(gpointer user_data, GObject *where_the_object_was)
{
  gpointer *data = user_data;

  g_source_remove(GPOINTER_TO_UINT(data[1]));
  free(data);
}

static gboolean
retry_power_cb(gpointer *data)
{
  g_object_weak_unref((GObject *)data[0], modem_disposed_cb, data);
  control_modem(data[0]);
  g_free(data);

  return FALSE;
}

static void
modem_powered_cb(OfonoModem *modem,const GError *error, void *arg)
{
  gboolean val = normal_mode ? TRUE : FALSE;

  if (error)
  {
    gboolean retry = FALSE;
    const char *path = ofono_modem_path(modem);

    if (g_dbus_error_is_remote_error(error))
    {
      gchar *err_name = g_dbus_error_get_remote_error(error);

      if (!g_strcmp0(err_name, "org.ofono.Error.InProgress"))
          retry = TRUE;

      g_free(err_name);
    }

    if (retry)
    {
      gpointer *data = g_new(gpointer, 2);
      guint id = g_timeout_add_seconds(5, (GSourceFunc)retry_power_cb, data);

      DLOG_DEBUG("modem %s has operation in progress, retrying...", path);

      data[0] = modem;
      data[1] = GUINT_TO_POINTER(id);
      g_object_weak_ref((GObject *)modem, modem_disposed_cb, data);
    }
    else
      DLOG_ERR("error setting modem %s power [%s]", path, error->message);
  }
  else
    online_modem(modem, val);
}

static void
control_modem(OfonoModem* modem)
{
  gboolean val = normal_mode ? TRUE : FALSE;
  const char *path = ofono_modem_path(modem);

  if (modem->powered != val)
  {
    DLOG_INFO("modem %s power set to %s", path, val ? "on" : "off");
    ofono_modem_set_powered_full(modem, val, modem_powered_cb, NULL);
  }
  else
    online_modem(modem, val);
}

static void
modem_valid_cb(OfonoModem* modem, void* arg)
{
  gpointer id;
  const char *path = ofono_modem_path(modem);

  if (g_hash_table_lookup_extended(ids, path, NULL, &id))
  {
    ofono_modem_remove_handler(modem, GPOINTER_TO_SIZE(id));
    g_hash_table_remove(ids, path);
  }

  control_modem(modem);
}

static void
queue_modem(OfonoModem* modem)
{
  if (ofono_modem_valid(modem))
    control_modem(modem);
  else
  {
    const char *path = ofono_modem_path(modem);

    DLOG_DEBUG("modem %s not ready, waiting to become valid.", path);

    gulong id = ofono_modem_add_valid_changed_handler(
          modem, modem_valid_cb, NULL);

    g_hash_table_insert(ids, g_strdup(path), GSIZE_TO_POINTER(id));
  }
}

static void
modem_added_cb(OfonoManager* manager, OfonoModem* modem, void* arg)
{
  DLOG_INFO("modem %s added", ofono_modem_path(modem));
  queue_modem(modem);
}

static void
modem_removed_cb(OfonoManager* manager, const char* path, void* arg)
{
  DLOG_INFO("modem %s removed", path);

  g_hash_table_remove(modems, path);
  g_hash_table_remove(ids, path);
}

static gboolean
idle_online_modems(gpointer data)
{
  GPtrArray *modems;
  guint i;

  if (!manager->valid)
    return FALSE;

  modems = ofono_manager_get_modems(manager);

  for (i = 0; i < modems->len; i++)
    queue_modem(modems->pdata[i]);

  return FALSE;
}

static void
manager_valid_cb(OfonoManager* manager, void* arg)
{
  DLOG_DEBUG("ofono manager become valid");

  g_idle_add_full(G_PRIORITY_HIGH_IDLE, idle_online_modems, NULL, NULL);
}

static void
ofono_manager_init()
{
  manager = ofono_manager_new();
  manager_valid_id = ofono_manager_add_valid_changed_handler(
        manager, manager_valid_cb, NULL);
  modem_added_id = ofono_manager_add_modem_added_handler(
        manager, modem_added_cb, NULL);
  modem_removed_id = ofono_manager_add_modem_removed_handler(
        manager, modem_removed_cb, NULL);

  modems = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                 (GDestroyNotify)ofono_modem_unref);
  ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

static void
ofono_manager_exit()
{
  g_hash_table_destroy(modems);
  g_hash_table_destroy(ids);
  ofono_manager_remove_handler(manager, modem_removed_id);
  ofono_manager_remove_handler(manager, modem_added_id);
  ofono_manager_remove_handler(manager, manager_valid_id);
  ofono_manager_unref(manager);
}

static void
set_mode(GVariant *v)
{
  gchar *mode = NULL;

  g_variant_get(v, "(s)", &mode);

  if (!g_strcmp0(mode, MCE_NORMAL_MODE))
    normal_mode = TRUE;
  else
    normal_mode = FALSE;

  g_free(mode);
}

static void
device_mode_ind_cb(GDBusConnection *connection, const gchar *sender_name,
                   const gchar *object_path, const gchar *interface_name,
                   const gchar *signal_name, GVariant *parameters,
                   gpointer user_data)
{
  set_mode(parameters);
  g_idle_add_full(G_PRIORITY_HIGH_IDLE, idle_online_modems, NULL, NULL);
}

static gboolean
get_device_mode_ind(GDBusConnection *system_bus)
{
  GVariant *v;
  GError *error = NULL;

  v = g_dbus_connection_call_sync(system_bus,
                                  MCE_SERVICE,
                                  MCE_REQUEST_PATH,
                                  MCE_REQUEST_IF,
                                  MCE_DEVICE_MODE_GET,
                                  NULL,
                                  NULL,
                                  G_DBUS_CALL_FLAGS_NONE,
                                  -1,
                                  NULL,
                                  &error);
  if (error)
  {
    DLOG_CRIT("g_dbus_connection_call_sync() failed: %s", error->message);
    g_error_free(error);
    return FALSE;
  }

  set_mode(v);
  g_variant_unref(v);

  return TRUE;
}

static struct option
long_options[] =
{
    {"nodetach", no_argument, NULL, 'n'},
    {NULL, 0, NULL, 0}
};

static void
usage()
{
  fprintf(stderr,
          "Usage: <program> [OPTIONS]\n"
          "Options:\n"
          "  -n, --nodetach\t\tDon't run as daemon in background\n"
          );
}

int
main(int argc, char *argv[])
{
  GError *error = NULL;
  GDBusConnection *system_bus;
  guint id;
  gboolean detach = TRUE;
  int opt;

  while ((opt = getopt_long(argc, argv, "n", long_options, NULL)) != -1)
  {
    switch (opt)
    {
      case 'n':
      {
        detach = FALSE;
        break;
      }
      default:
      {
        usage();
        exit(1);
      }
    }
  }

  DLOG_OPEN(PACKAGE);

  if (detach && daemon(TRUE, TRUE) != 0)
  {
    DLOG_CRIT("Could not run as daemon: %d", errno);
    LOG_CLOSE();
    exit(1);
  }

  system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

  if (system_bus == NULL)
  {
      DLOG_CRIT("Could not get dbus system session bus: %s",
                error->message);
      g_error_free(error);
      LOG_CLOSE();
      exit(1);
  }

  id = g_dbus_connection_signal_subscribe(system_bus, NULL,
                                          MCE_SIGNAL_IF,
                                          MCE_DEVICE_MODE_SIG,
                                          MCE_SIGNAL_PATH,
                                          NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                          device_mode_ind_cb, NULL, NULL);

  if (get_device_mode_ind(system_bus))
  {
    loop = g_main_loop_new(NULL, TRUE);
    ofono_manager_init();
    g_main_loop_run(loop);
    ofono_manager_exit();
    g_object_unref(loop);
  }

  g_dbus_connection_signal_unsubscribe(system_bus, id);

  g_object_unref(system_bus);
  LOG_CLOSE();

  return 0;
}

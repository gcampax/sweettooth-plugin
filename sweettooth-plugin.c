/* Todo: Insert copyright notice here */

#include <string.h>

#include <npapi.h>
#include <npruntime.h>
#include <npfunctions.h>

#include <glib.h>
#include <gio/gio.h>

#define ORIGIN "http://extensions.gnome.org"
#define ALLOW_FOREIGN_ORIGIN 1
#define PLUGIN_NAME "Gnome Shell Integration"
#define PLUGIN_DESCRIPTION "This plugin provides integration with Gnome Shell " \
      "for live extension enabling and disabling. " \
      "It can be used only by extensions.gnome.org"
#define PLUGIN_MIME_STRING "application/x-gnome-shell-integration::Gnome Shell Integration Dummy Content-Type";

/* the provided one can be used only with C++ */
#undef STRINGZ_TO_NPVARIANT
#define STRINGZ_TO_NPVARIANT(_val, _v)                                        \
NP_BEGIN_MACRO                                                                \
    (_v).type = NPVariantType_String;                                         \
    NPString str = { _val, (uint32_t)(strlen(_val)) };	                      \
    (_v).value.stringValue = str;                                             \
NP_END_MACRO

typedef struct {
  GDBusProxy *proxy;
} PluginData;

/* =============== public entry points =================== */

NPNetscapeFuncs funcs;

NPError
NP_Initialize(NPNetscapeFuncs *pfuncs, NPPluginFuncs *plugin)
{
  /* global initialization routine, called once when plugin
     is loaded */

  g_debug ("plugin loaded");

  memcpy (&funcs, pfuncs, sizeof (funcs));

  plugin->size = sizeof(NPPluginFuncs);
  plugin->newp = NPP_New;
  plugin->destroy = NPP_Destroy;
  plugin->getvalue = NPP_GetValue;

  return NPERR_NO_ERROR;
}

NPError
NP_Shutdown(void)
{
  /* global finalization routine, called when plugin is unloaded */

  g_debug ("plugin unloaded");

  return NPERR_NO_ERROR;
}

char*
NP_GetMIMEDescription(void)
{
  return PLUGIN_MIME_STRING;

  g_debug ("plugin queried");
}

NPError
NP_GetValue(void         *instance,
	    NPPVariable   variable,
	    void         *value)
{
  switch (variable) {
  case NPPVpluginNameString:
    *(char**)value = PLUGIN_NAME;
    break;
  case NPPVpluginDescriptionString:
    *(char**)value = PLUGIN_DESCRIPTION;
    break;
  default:
    ;
  }

  return NPERR_NO_ERROR;
}

NPError
NPP_New(NPMIMEType    mimetype,
	NPP           instance,
	uint16_t      mode,
	int16_t       argc,
	char        **argn,
	char        **argv,
	NPSavedData  *saved)
{
  /* instance initialization function */
  NPError ret = NPERR_NO_ERROR;
  PluginData *data;
  GError *error = NULL;

  /* first, check if the location is what we expect */
  NPObject *window;
  NPVariant document;
  NPVariant location;
  NPVariant href;

  g_debug ("plugin created");

  funcs.getvalue (instance, NPNVWindowNPObject, &window);

  funcs.getproperty (instance, window,
		     funcs.getstringidentifier ("document"),
		     &document);
  g_assert (NPVARIANT_IS_OBJECT (document));

  funcs.getproperty (instance, NPVARIANT_TO_OBJECT (document),
		     funcs.getstringidentifier ("location"),
		     &location);
  g_assert (NPVARIANT_IS_OBJECT (location));

  funcs.getproperty (instance, NPVARIANT_TO_OBJECT (location),
		     funcs.getstringidentifier ("href"),
		     &href);
  g_assert (NPVARIANT_IS_STRING (href));

  if (strncmp (NPVARIANT_TO_STRING (href).UTF8Characters, ORIGIN, sizeof (ORIGIN)-1))
    {
      g_debug ("origin does not match, is %s", NPVARIANT_TO_STRING (href).UTF8Characters);

      if (!ALLOW_FOREIGN_ORIGIN)
	{
	  ret = NPERR_GENERIC_ERROR;
	  goto out;
	}
    }

  data = g_slice_new (PluginData);
  instance->pdata = data;

  data->proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					       G_DBUS_PROXY_FLAGS_NONE,
					       NULL, /* interface info */
					       "org.gnome.Shell",
					       "/org/gnome/Shell",
					       "org.gnome.Shell",
					       NULL, /* GCancellable */
					       &error);
  if (!data->proxy) {
    /* ignore error if the shell is not running, otherwise warn */
    if (error->domain != G_DBUS_ERROR ||
	error->code != G_DBUS_ERROR_NAME_HAS_NO_OWNER)
      {
	g_warning ("Failed to set up Shell proxy: %s", error->message);
	g_error_clear (&error);
      }
    ret = NPERR_GENERIC_ERROR;
  }

  g_debug ("plugin created successfully");

 out:
  funcs.releaseobject (window);
  funcs.releasevariantvalue (&document);
  funcs.releasevariantvalue (&location);
  funcs.releasevariantvalue (&href);

  return ret;
}

NPError
NPP_Destroy(NPP           instance,
	    NPSavedData **saved)
{
  /* instance finalization function */

  PluginData *data = instance->pdata;

  g_debug ("plugin destroyed");

  g_object_unref (data->proxy);

  return NPERR_NO_ERROR;
}

/* =================== scripting interface =================== */

typedef struct {
  NPObject     parent;
  NPP          instance;
  GDBusProxy  *proxy;
  NPObject    *listener;
  gint         signal_id;
} PluginObject;

static void
on_shell_signal (GDBusProxy *proxy,
		 gchar      *sender_name,
		 gchar      *signal_name,
		 GVariant   *parameters,
		 gpointer    user_data)
{
  PluginObject *obj = user_data;

  if (strcmp (signal_name, "ExtensionStatusChanged") == 0)
    {
      gchar *uuid;
      gint32 status;
      gchar *error;
      NPVariant args[3];
      NPVariant result;

      g_variant_get (parameters, "(sis)", &uuid, &status, &error);
      STRINGZ_TO_NPVARIANT (uuid, args[0]);
      INT32_TO_NPVARIANT (status, args[1]);
      STRINGZ_TO_NPVARIANT (error, args[2]);

      funcs.invokeDefault (obj->instance, obj->listener,
			   args, 3, &result);

      funcs.releasevariantvalue (&result);
      g_free (uuid);
      g_free (error);
    }
}

static NPObject *
plugin_object_allocate (NPP      instance,
			NPClass *klass)
{
  PluginData *data = instance->pdata;
  PluginObject *obj = g_slice_new0 (PluginObject);

  obj->instance = instance;
  obj->proxy = g_object_ref (data->proxy);
  obj->signal_id = g_signal_connect (obj->proxy, "g-signal",
				     G_CALLBACK (on_shell_signal), obj);

  g_debug ("plugin object created");

  return (NPObject*)obj;
}

static void
plugin_object_deallocate (NPObject *npobj)
{
  PluginObject *obj = (PluginObject*)npobj;

  g_signal_handler_disconnect (obj->proxy, obj->signal_id);
  g_object_unref (obj->proxy);

  if (obj->listener)
    funcs.releaseobject (obj->listener);

  g_debug ("plugin object destroyed");

  g_slice_free (PluginObject, obj);
}

static NPIdentifier get_metadata_id;
static NPIdentifier list_extensions_id;
static NPIdentifier enable_extension_id;
static NPIdentifier install_extension_id;
static NPIdentifier onextension_changed_id;

static bool
plugin_object_has_method (NPObject     *npobj,
			  NPIdentifier  name)
{
  return (name == get_metadata_id ||
	  name == list_extensions_id ||
	  name == enable_extension_id ||
	  name == install_extension_id);
}

static bool
plugin_list_extensions (PluginObject  *obj,
			NPVariant     *result)
{
  GError *error = NULL;
  GVariant *res;
  const gchar *json;
  gchar *buffer;

  res = g_dbus_proxy_call_sync (obj->proxy,
				"ListExtensions",
				NULL, /* parameters */
				G_DBUS_CALL_FLAGS_NONE,
				-1, /* timeout */
				NULL, /* cancellable */
				&error);

  if (!res)
    {
      g_warning ("Failed to retrieve extension list: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  g_variant_get(res, "(&s)", &json);

  buffer = funcs.memalloc (strlen (json));
  strcpy(buffer, json);

  STRINGZ_TO_NPVARIANT (buffer, *result);
  g_variant_unref (res);

  return TRUE;
}

static bool
plugin_enable_extension (PluginObject *obj,
			 NPString      uuid,
			 gboolean      enabled)
{
  g_dbus_proxy_call (obj->proxy,
		     "SetExtensionEnabled",
		     g_variant_new ("(sb)", uuid.UTF8Characters, enabled),
		     G_DBUS_CALL_FLAGS_NONE,
		     -1, /* timeout */
		     NULL, /* cancellable */
		     NULL, /* callback */
		     NULL /* user_data */);

  return TRUE;
}

static bool
plugin_install_extension (PluginObject *obj,
			  NPString      uuid)
{
  g_dbus_proxy_call (obj->proxy,
		     "InstallRemoteExtension",
		     g_variant_new ("(s)", uuid.UTF8Characters),
		     G_DBUS_CALL_FLAGS_NONE,
		     -1, /* timeout */
		     NULL, /* cancellable */
		     NULL, /* callback */
		     NULL /* user_data */);

  return TRUE;
}

static bool
plugin_get_metadata (PluginObject *obj,
		     NPString      uuid,
		     NPVariant    *result)
{
  GError *error = NULL;
  GVariant *res;
  const gchar *json;
  gchar *buffer;

  res = g_dbus_proxy_call_sync (obj->proxy,
				"GetExtensionInfo",
				g_variant_new ("(s)", uuid.UTF8Characters),
				G_DBUS_CALL_FLAGS_NONE,
				-1, /* timeout */
				NULL, /* cancellable */
				&error);

  if (!res)
    {
      g_warning ("Failed to retrieve extension list: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  g_variant_get(res, "(&s)", &json);

  buffer = funcs.memalloc (strlen (json));
  strcpy(buffer, json);

  STRINGZ_TO_NPVARIANT (buffer, *result);
  g_variant_unref (res);

  return TRUE;
}

static bool
plugin_object_invoke (NPObject        *npobj,
		      NPIdentifier     name,
		      const NPVariant *args,
		      uint32_t         argc,
		      NPVariant       *result)
{
  PluginObject *obj;

  g_return_val_if_fail (plugin_object_has_method (npobj, name), FALSE);

  g_debug ("invoking plugin object method");

  obj = (PluginObject*) npobj;

  VOID_TO_NPVARIANT (*result);

  if (name == list_extensions_id)
    return plugin_list_extensions (obj, result);
  else if (name == get_metadata_id)
    {
      g_return_val_if_fail (argc >= 1, FALSE);
      g_return_val_if_fail (NPVARIANT_IS_STRING(args[0]), FALSE);

      return plugin_get_metadata (obj, NPVARIANT_TO_STRING(args[0]), result);
    }
  else if (name == enable_extension_id)
    {
      g_return_val_if_fail (argc >= 2, FALSE);
      g_return_val_if_fail (NPVARIANT_IS_STRING(args[0]), FALSE);
      g_return_val_if_fail (NPVARIANT_IS_BOOLEAN(args[1]), FALSE);

      return plugin_enable_extension (obj,
				      NPVARIANT_TO_STRING(args[0]),
				      NPVARIANT_TO_BOOLEAN(args[1]));
    }
  else if (name == install_extension_id)
    {
      g_return_val_if_fail (argc >= 1, FALSE);
      g_return_val_if_fail (NPVARIANT_IS_STRING(args[0]), FALSE);

      return plugin_install_extension (obj,
				       NPVARIANT_TO_STRING(args[0]));
    }

  return TRUE;
}

static bool
plugin_object_has_property (NPObject     *npobj,
			    NPIdentifier  name)
{
  return name == onextension_changed_id;
}

static bool
plugin_object_get_property (NPObject     *npobj,
			    NPIdentifier  name,
			    NPVariant    *result)
{
  PluginObject *obj;

  g_return_val_if_fail (plugin_object_has_property (npobj, name), FALSE);

  obj = (PluginObject*) npobj;
  if (obj->listener)
    OBJECT_TO_NPVARIANT (obj->listener, *result);
  else
    NULL_TO_NPVARIANT (*result);

  return TRUE;
}

static bool
plugin_object_set_property (NPObject        *npobj,
			    NPIdentifier     name,
			    const NPVariant *value)
{
  PluginObject *obj;

  g_return_val_if_fail (plugin_object_has_property (npobj, name), FALSE);

  obj = (PluginObject*) npobj;
  if (obj->listener)
    funcs.releaseobject (obj->listener);

  obj->listener = NULL;
  if (NPVARIANT_IS_OBJECT (*value))
    {
      obj->listener = NPVARIANT_TO_OBJECT (*value);
      funcs.retainobject (obj->listener);
      return TRUE;
    }
  else if (NPVARIANT_IS_NULL (*value))
    return TRUE;

  return FALSE;
}

static NPClass plugin_class = {
  NP_CLASS_STRUCT_VERSION,
  plugin_object_allocate,
  plugin_object_deallocate,
  NULL, /* invalidate */
  plugin_object_has_method,
  plugin_object_invoke,
  NULL, /* invoke default */
  plugin_object_has_property,
  plugin_object_get_property,
  plugin_object_set_property,
  NULL, /* remove property */
  NULL, /* enumerate */
  NULL, /* construct */
};

static void
init_methods_and_properties (void)
{
  /* this is the JS public API; it is manipulated through NPIdentifiers for speed */
  get_metadata_id = funcs.getstringidentifier ("getExtensionMetadata");
  list_extensions_id = funcs.getstringidentifier ("listExtensions");
  enable_extension_id = funcs.getstringidentifier ("setExtensionEnabled");
  install_extension_id = funcs.getstringidentifier ("installExtension");

  onextension_changed_id = funcs.getstringidentifier ("onchange");
}

NPError
NPP_GetValue(NPP          instance,
	     NPPVariable  variable,
	     void        *value)
{
  PluginData *data = instance->pdata;

  g_debug ("NPP_GetValue called");

  switch (variable) {
  case NPPVpluginScriptableNPObject:
    g_debug ("creating scriptable object");
    init_methods_and_properties ();

    *(NPObject**)value = funcs.createobject (instance, &plugin_class);
    break;
  default:
    ;
  }

  return NPERR_NO_ERROR;
}

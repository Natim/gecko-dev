const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Timer.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "FileUtils",
                                  "resource://gre/modules/FileUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "OS",
                                  "resource://gre/modules/osfile.jsm");

const PREF_BLOCKLIST_VIA_AMO = "security.blocklist.via.amo";
const KEY_APPDIR             = "XCurProcD";
const KEY_PROFILEDIR         = "ProfD";
const TEST_APP_ID            = "xpcshell@tests.mozilla.org";


const SAMPLE_FILE = do_get_file("data/test_blocklist_kinto/sample.json");

const SAMPLE_ADDON_RECORD = {
  "prefs": [],
  "blockID": "i446",
  "last_modified": 1457434834683,
  "versionRange": [{
    "targetApplication": [{
      "minVersion": "0.1",
      "guid": "{ec8030f7-c20a-464f-9b0e-13a3a9e97384}",
      "maxVersion": "17.*"
    }],
    "maxVersion": "*",
    "minVersion": "0",
    "severity": "1",
    "vulnerabilityStatus": "0"
  }],
  "guid": "{E90FA778-C2B7-41D0-9FA9-3FEC1CA54D66}",
  "id": "87a5dc56-1fec-ebf2-a09b-6f2cbd4eb2d3"
};

const SAMPLE_PLUGIN_RECORD = {
  "matchFilename": "JavaPlugin2_NPAPI\\.plugin",
  "blockID": "p123",
  "id": "bdcf0717-a873-adbf-7603-83a49fb996bc",
  "last_modified": 1457434851748,
  "versionRange": [{
    "targetApplication": [{
      "minVersion": "0.1",
      "guid": "{ec8030f7-c20a-464f-9b0e-13a3a9e97384}",
      "maxVersion": "17.*"
    }],
    "maxVersion": "14.2.0",
    "minVersion": "0",
    "severity": "1"
  }]
};

const SAMPLE_GFX_RECORD = {
  "driverVersionComparator": "LESS_THAN_OR_EQUAL",
  "driverVersion": "8.17.12.5896",
  "vendor": "0x10de",
  "blockID": "g36",
  "feature": "DIRECT3D_9_LAYERS",
  "devices": ["0x0a6c"],
  "featureStatus": "BLOCKED_DRIVER_VERSION",
  "last_modified": 1458035931837,
  "os": "WINNT 6.1",
  "id": "3f947f16-37c2-4e96-d356-78b26363729b"
};


function clearProfile(name) {
  let filename = "blocklist-" + name + ".json";
  let blocklist = FileUtils.getFile(KEY_PROFILEDIR, [filename]);
  if (blocklist.exists())
    blocklist.remove(true);
}


function copyToProfile(file, name) {
  const gProfDir = FileUtils.getFile(KEY_PROFILEDIR, []);
  let filename = "blocklist-" + name + ".json";
  file = file.clone();
  file.copyTo(gProfDir, filename);
  file = gProfDir.clone();
  file.append(filename);
}


function Blocklist() {
  let blocklist = Cc["@mozilla.org/extensions/blocklist;1"].
                  getService().wrappedJSObject;
  blocklist._clear();
  return blocklist;
}


function run_test() {
  // Some blocklist code rely on gApp.ID.
  createAppInfo(TEST_APP_ID, "XPCShell", "1", "1");
  // Disable blocklist via AMO.
  Services.prefs.setBoolPref(PREF_BLOCKLIST_VIA_AMO, false);

  // Starts addons manager.
  startupManager();

  // Clean-up for profile data.
  do_register_cleanup(function() {
    for(let filename of ['blocklist-addons.json', 'blocklist-plugins.json']) {
      const file = FileUtils.getFile(KEY_PROFILEDIR, [filename]);
      if (file.exists()) file.remove(true);
    }
  });

  run_next_test();
}


add_task(function* test_addon_entry_from_json_simple() {
  const blocklist = Blocklist();
  const data = Object.assign({}, SAMPLE_ADDON_RECORD);

  const entry = blocklist._handleAddonItemJSON(data);

  equal(entry.blockID, SAMPLE_ADDON_RECORD.blockID);
  equal(entry.prefs, SAMPLE_ADDON_RECORD.prefs);
  equal(entry.attributes.get("id"), SAMPLE_ADDON_RECORD.guid);
  equal(entry.versions.length, 1);
  const item = entry.versions[0];
  equal(item.minVersion, "0");
  equal(item.maxVersion, "*");
  equal(item.severity, "1");
  equal(item.vulnerabilityStatus, "0");
  equal(item.targetApps["{ec8030f7-c20a-464f-9b0e-13a3a9e97384}"].minVersion, "0.1");
  equal(item.targetApps["{ec8030f7-c20a-464f-9b0e-13a3a9e97384}"].maxVersion, "17.*");
});


add_task(function* test_addon_entry_from_json_no_version_range() {
  const blocklist = Blocklist();
  const data = Object.assign({}, SAMPLE_ADDON_RECORD);
  data.versionRange = [];

  const entry = blocklist._handleAddonItemJSON(data);

  equal(entry.versions.length, 1);
  const item = entry.versions[0];
  equal(item.minVersion, null);
  equal(item.maxVersion, null);
  equal(item.severity, 3);
  equal(item.vulnerabilityStatus, 0);
  equal(item.targetApps[TEST_APP_ID].minVersion, null);
  equal(item.targetApps[TEST_APP_ID].maxVersion, null);
});


add_task(function* test_addon_entry_from_json_without_blockid() {
  const blocklist = Blocklist();
  const data = Object.assign({}, SAMPLE_ADDON_RECORD);
  delete data.blockID;

  const entry = blocklist._handleAddonItemJSON(data);

  equal(entry.blockID, SAMPLE_ADDON_RECORD.id);
});


add_task(function* test_addon_entry_without_target_application() {
  const blocklist = Blocklist();
  const data = Object.assign({}, SAMPLE_ADDON_RECORD);
  data.versionRange[0].targetApplication = [];

  const entry = blocklist._handleAddonItemJSON(data);

  const item = entry.versions[0];
  equal(item.minVersion, "0");
  equal(item.maxVersion, "*");
  equal(item.severity, "1");
  equal(item.targetApps[TEST_APP_ID].minVersion, null);
  equal(item.targetApps[TEST_APP_ID].maxVersion, null);
});


add_task(function* test_plugin_entry_from_json_simple() {
  const blocklist = Blocklist();
  const data = Object.assign({}, SAMPLE_PLUGIN_RECORD);

  const entry = blocklist._handlePluginItemJSON(data);

  equal(entry.blockID, SAMPLE_PLUGIN_RECORD.blockID);
  equal(entry.infoURL, SAMPLE_PLUGIN_RECORD.infoURL);
  equal(entry.matches['filename'].constructor.name, "RegExp");
  equal(entry.versions.length, 1);
  const item = entry.versions[0];
  equal(item.minVersion, "0");
  equal(item.maxVersion, "14.2.0");
  equal(item.severity, "1");
  equal(item.targetApps["{ec8030f7-c20a-464f-9b0e-13a3a9e97384}"].minVersion, "0.1");
  equal(item.targetApps["{ec8030f7-c20a-464f-9b0e-13a3a9e97384}"].maxVersion, "17.*");
});


add_task(function* test_plugin_entry_from_json_no_match() {
  const blocklist = Blocklist();
  const data = Object.assign({}, SAMPLE_PLUGIN_RECORD);
  delete data.matchFilename;

  const entry = blocklist._handlePluginItemJSON(data);

  equal(entry, undefined);
});


add_task(function* test_plugin_entry_from_json_no_version_range() {
  const blocklist = Blocklist();
  const data = Object.assign({}, SAMPLE_PLUGIN_RECORD);
  data.versionRange = [];

  const entry = blocklist._handlePluginItemJSON(data);

  equal(entry.versions.length, 1);
  const item = entry.versions[0];
  equal(item.minVersion, null);
  equal(item.maxVersion, null);
  equal(item.severity, 3);
  equal(item.targetApps[TEST_APP_ID].minVersion, null);
  equal(item.targetApps[TEST_APP_ID].maxVersion, null);
});


add_task(function* test_plugin_entry_from_json_without_blockid() {
  const blocklist = Blocklist();
  const data = Object.assign({}, SAMPLE_PLUGIN_RECORD);
  delete data.blockID;

  const entry = blocklist._handlePluginItemJSON(data);

  equal(entry.blockID, SAMPLE_PLUGIN_RECORD.id);
});


add_task(function* test_plugin_entry_without_target_application() {
  const blocklist = Blocklist();
  const data = Object.assign({}, SAMPLE_PLUGIN_RECORD);
  data.versionRange[0].targetApplication = [];

  const entry = blocklist._handlePluginItemJSON(data);

  const item = entry.versions[0];
  equal(item.minVersion, "0");
  equal(item.maxVersion, "14.2.0");
  equal(item.severity, "1");
  equal(item.targetApps[TEST_APP_ID].minVersion, null);
  equal(item.targetApps[TEST_APP_ID].maxVersion, null);
});


add_task(function* test_is_loaded_synchronously() {
  const blocklist = Blocklist();
  strictEqual(blocklist._isBlocklistLoaded(), false);
  // Calls synchronous method from Interface.
  blocklist.isAddonBlocklisted("addon", "appVersion", "toolkitVersion");
  strictEqual(blocklist._isBlocklistLoaded(), true);
});


add_task(function* test_notify_does_not_download_xml_file() {
  const blocklist = Blocklist();
  strictEqual(blocklist._isBlocklistLoaded(), false);
  // When managed with Kinto, nothing is loaded/downloaded on notify.
  blocklist.notify(null);
  strictEqual(blocklist._isBlocklistLoaded(), false);
});


add_task(function* preload_json_async() {
  const blocklist = Blocklist();

  yield blocklist._preloadBlocklist();

  // Preloaded content comes from app dir.
  const addonsAppPath = FileUtils.getFile(KEY_APPDIR, ['blocklist-addons.json']).path;
  const gfxAppPath = FileUtils.getFile(KEY_APPDIR, ['blocklist-gfx.json']).path;
  const pluginsAppPath = FileUtils.getFile(KEY_APPDIR, ['blocklist-plugins.json']).path;
  strictEqual(blocklist._preloadedBlocklistContent[addonsAppPath].length > 0, true);
  strictEqual(blocklist._preloadedBlocklistContent[gfxAppPath].length > 0, true);
  strictEqual(blocklist._preloadedBlocklistContent[pluginsAppPath].length > 0, true);
});


add_task(function* preload_json_reads_profile_data() {
  const blocklist = Blocklist();

  // Write some JSON content in profile dir.
  copyToProfile(SAMPLE_FILE, "addons");

  try {
    yield blocklist._preloadBlocklist();
    // Preloaded content comes from profile dir file.
    const path = FileUtils.getFile(KEY_PROFILEDIR, ['blocklist-addons.json']).path;
    let fileContent = blocklist._preloadedBlocklistContent[path];
    strictEqual(fileContent.length > 0, true);
    strictEqual(JSON.parse(fileContent).data[0].blockID, "i53923");
  }
  finally {
    // Clean-up: delete created file.
    clearProfile("addons");
  }
});


add_task(function* load_uses_preloaded_json_if_available() {
  const blocklist = Blocklist();

  // Simulate preload of data.
  const addonsAppPath = FileUtils.getFile(KEY_APPDIR, ['blocklist-addons.json']).path;
  const gfxAppPath = FileUtils.getFile(KEY_APPDIR, ['blocklist-gfx.json']).path;
  const pluginsAppPath = FileUtils.getFile(KEY_APPDIR, ['blocklist-plugins.json']).path;
  blocklist._preloadedBlocklistContent[addonsAppPath] = JSON.stringify({data: [SAMPLE_ADDON_RECORD]});
  blocklist._preloadedBlocklistContent[gfxAppPath] = JSON.stringify({data: [SAMPLE_GFX_RECORD]});
  blocklist._preloadedBlocklistContent[pluginsAppPath] = JSON.stringify({data: [SAMPLE_PLUGIN_RECORD]});

  blocklist._loadBlocklist();

  // Data loaded comes from preloaded content.
  equal(blocklist._addonEntries[0].blockID, "i446");
  equal(blocklist._gfxEntries[0].blockID, "g36");
  equal(blocklist._pluginEntries[0].blockID, "p123");
});


add_task(function* test_blocklist_is_reloaded_when_message_is_received() {
  const blocklist = Blocklist();

  blocklist._loadBlocklist();

  // Write a fake JSON file in profile dir.
  copyToProfile(SAMPLE_FILE, "addons");

  // Send message.
  yield Services.cpmm.sendAsyncMessage("Blocklist:reload-from-disk");

  // Since message is loaded asynchronously, wait for its reception.
  yield new Promise((resolve) => setTimeout(() => resolve(), 100));

  // Blocklist was reloaded with the content from file.
  equal(blocklist._addonEntries[0].blockID, "i53923");
});


add_task(function* test_read_json_from_app_or_profile() {
  const blocklist = Blocklist();

  // Reads from app dir by default.
  clearProfile("addons");
  blocklist._loadBlocklist();
  equal(blocklist._addonEntries.length, 416);

  // Reads from profile if present.
  copyToProfile(SAMPLE_FILE, "addons");
  try {
    blocklist._loadBlocklist();
    equal(blocklist._addonEntries.length, 3);
  }
  finally {
    clearProfile("addons");
  }
});


add_task(function* test_invalid_preloaded_json() {
  const blocklist = Blocklist();

  // Simulate invalid json in preloaded data from app dir:
  const addonsAppPath = FileUtils.getFile(KEY_APPDIR, ['blocklist-addons.json']).path;
  blocklist._preloadedBlocklistContent[addonsAppPath] = '{>invalid}';

  blocklist._loadBlocklist();

  // XXX: What should we do? Fallback on release file? Raise loudly?
  // Current behaviour with XML is to give up like this:
  deepEqual(blocklist._addonEntries, []);
});


add_task(function* test_invalid_json_file() {
  // Write invalid JSON file in profile (as failed download or whatever)
  const path = OS.Path.join(OS.Constants.Path.profileDir, 'blocklist-addons.json');
  yield OS.File.writeAtomic(path, '{>invalid}', {tmpPath: path + ".tmp"});

  const blocklist = Blocklist();
  try {
    blocklist._loadBlocklist();
    // XXX: What should we do? Fallback on release file? Raise loudly?
    // Current behaviour with XML is to give up like this:
    deepEqual(blocklist._addonEntries.length, 0);
  }
  finally {
    clearProfile("addons");
  }
});

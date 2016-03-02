/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = ["PluginBlocklistClient"];

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;

Cu.import("resource://services-common/moz-kinto-client.js");

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Task.jsm");

const PREF_KINTO_BASE = "services.kinto.base";
const PREF_KINTO_BUCKET = "services.kinto.bucket";
const PREF_KINTO_PLUGINS_COLLECTION = "services.kinto.plugins.collection";
const PREF_KINTO_PLUGINS_CHECKED_SECONDS = "services.kinto.plugins.checked";

//XXX debug/hack:
const console = (Components.utils.import("resource://gre/modules/Console.jsm", {})).console;

// A Kinto based client to keep the Plugins blocklist up to date.
function PluginBlocklistClient() {
  const base = Services.prefs.getCharPref(PREF_KINTO_BASE);
  const bucket = Services.prefs.getCharPref(PREF_KINTO_BUCKET);

  const Kinto = loadKinto();

  const FirefoxAdapter = Kinto.adapters.FirefoxAdapter;

  // Future blocklist clients can extract the sync-if-stale logic. For
  // now, since this is currently the only client, we'll do this here.
  const config = {
    remote: base,
    bucket: bucket,
    adapter: FirefoxAdapter,
  };

  const db = new Kinto(config);
  const collectionName = Services.prefs.getCharPref(PREF_KINTO_PLUGINS_COLLECTION,
                                                    "plugins");
  const blocklist = db.collection(collectionName);

  this.loadList = function () {
    console.log('loadList()');
    var records;
    return blocklist.db.open()
      .then(() => blocklist.list())
      .then((result) => { records = result.data; })
      .then(blocklist.db.close())
      .then(() => records);
  };

  // maybe sync the collection of certificates with remote data.
  // lastModified - the lastModified date (on the server, milliseconds since
  // epoch) of data in the remote collection
  // serverTime - the time on the server (milliseconds since epoch)
  // returns a promise which rejects on sync failure
  this.maybeSync = function(lastModified, serverTime) {
    let updateLastCheck = function() {
      let checkedServerTimeInSeconds = Math.round(serverTime / 1000);
      Services.prefs.setIntPref(PREF_KINTO_PLUGINS_CHECKED_SECONDS,
                                checkedServerTimeInSeconds);
    }

    return Task.spawn(function* () {
      try {
        yield blocklist.db.open();
        let collectionLastModified = yield blocklist.db.getLastModified();
        // if the data is up to date, there's no need to sync. We still need
        // to record the fact that a check happened.
        if (lastModified <= collectionLastModified) {
          updateLastCheck();
          return;
        }
        // sync from server.
        yield blocklist.sync();
        let list = yield blocklist.list();
        for (let item of list.data) {
          // Update plugins blocklist
        }
        updateLastCheck();
      } finally {
        blocklist.db.close();
      }
    });
  }
}

this.PluginBlocklistClient = new PluginBlocklistClient();

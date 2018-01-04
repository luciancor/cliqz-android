/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Service workers can't be loaded from chrome://,
// but http:// is ok with dom.serviceWorkers.testing.enabled turned on.
const SERVICE_WORKER = URL_ROOT + "service-workers/delay-sw.js";
const TAB_URL = URL_ROOT + "service-workers/delay-sw.html";
const SW_TIMEOUT = 2000;

requestLongerTimeout(2);

add_task(function* () {
  yield enableServiceWorkerDebugging();
  yield pushPref("dom.serviceWorkers.idle_timeout", SW_TIMEOUT);
  yield pushPref("dom.serviceWorkers.idle_extended_timeout", SW_TIMEOUT);

  let { tab, document } = yield openAboutDebugging("workers");

  // Listen for mutations in the service-workers list.
  let serviceWorkersElement = getServiceWorkerList(document);
  let onMutation = waitForMutation(serviceWorkersElement, { childList: true });

  let swTab = yield addTab(TAB_URL);

  info("Make the test page notify us when the service worker sends a message.");

  // Wait for the service-workers list to update.
  yield onMutation;

  // Check that the service worker appears in the UI
  let names = [...document.querySelectorAll("#service-workers .target-name")];
  let name = names.filter(element => element.textContent === SERVICE_WORKER)[0];
  ok(name, "Found the service worker in the list");

  let targetElement = name.parentNode.parentNode;
  let status = targetElement.querySelector(".target-status");

  // We should ideally check that the service worker registration goes through the
  // "registering" and "running" steps, but it is difficult to workaround race conditions
  // for a test running on a wide variety of platforms. Due to intermittent failures, we
  // simply check that the registration transitions to "stopped".
  yield waitUntil(() => status.textContent == "Stopped", 100);
  is(status.textContent, "Stopped", "Service worker is currently stopped");

  try {
    yield unregisterServiceWorker(swTab, serviceWorkersElement);
    ok(true, "Service worker unregistered");
  } catch (e) {
    ok(false, "Service worker not unregistered; " + e);
  }

  // Check that the service worker disappeared from the UI
  names = [...document.querySelectorAll("#service-workers .target-name")];
  names = names.map(element => element.textContent);
  ok(!names.includes(SERVICE_WORKER),
    "The service worker url is no longer in the list: " + names);

  yield removeTab(swTab);
  yield closeAboutDebugging(tab);
});
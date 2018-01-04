#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

cd /setup

. /setup/common.sh
. /setup/install-mercurial.sh
. /setup/install-make.sh
. /setup/install-cmake.sh

if [ -f /setup/install-debug-symbols.sh ]; then
    . /setup/install-debug-symbols.sh
fi

rm -rf /setup
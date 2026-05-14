// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    width: 240
    height: 160
    visible: true

    TextInput {
        objectName: "fixture.target"
        width: 140
        height: 32
        text: "locked"
        readOnly: true
    }
}

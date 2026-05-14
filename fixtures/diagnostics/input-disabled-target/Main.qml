// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    width: 240
    height: 160
    visible: true

    MouseArea {
        objectName: "fixture.target"
        x: 24
        y: 40
        width: 100
        height: 40
        enabled: false
    }
}

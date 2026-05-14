// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    width: 240
    height: 160
    visible: true

    Rectangle {
        width: 140
        height: 40
        color: "#1f6feb"

        MouseArea {
            objectName: "fixture.action"
            anchors.fill: parent
            onClicked: {
                // Intentionally no state change. The workflow client should
                // report that input was delivered but the expected result did
                // not change.
            }
        }
    }

    Rectangle {
        objectName: "fixture.result"
        y: 64
        width: 120
        height: 32
        color: "#2f7d32"
        visible: false
    }
}

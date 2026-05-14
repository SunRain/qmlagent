// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    width: 240
    height: 180
    visible: true

    Item {
        objectName: "fixture.form"
        anchors.fill: parent

        Rectangle {
            objectName: "fixture.header"
            x: 24
            y: 24
            width: 120
            height: 32
            color: "#8ecae6"
        }

        Item {
            objectName: "fixture.spacer"
            y: 70
            width: 1
            height: 100
        }

        Rectangle {
            objectName: "fixture.target"
            x: 24
            y: 190
            width: 80
            height: 30
            color: "#1f6feb"
        }
    }
}

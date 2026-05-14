// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    width: 240
    height: 160
    visible: true

    Item {
        objectName: "fixture.clip"
        x: 24
        y: 32
        width: 64
        height: 48
        clip: true

        Rectangle {
            objectName: "fixture.target"
            x: 44
            y: 10
            width: 56
            height: 24
            color: "#fb8500"
        }
    }
}

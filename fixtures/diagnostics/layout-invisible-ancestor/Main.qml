// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    width: 240
    height: 160
    visible: true

    Item {
        objectName: "fixture.hiddenAncestor"
        visible: false
        width: 160
        height: 80

        Rectangle {
            objectName: "fixture.target"
            width: 80
            height: 30
            color: "#4f7cff"
        }
    }
}

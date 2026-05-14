// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    id: root
    width: 320
    height: 240
    visible: true

    property int baseValue: 41

    Rectangle {
        id: visualTarget
        property int computedValue: root.baseValue + 1

        x: computedValue
        y: 20
        width: 40
        height: 24
        color: "#0969da"
    }
}

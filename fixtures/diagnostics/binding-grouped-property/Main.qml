// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    id: root
    width: 320
    height: 240
    visible: true

    property int gutter: 14

    Rectangle {
        id: groupedBindingTarget
        width: 80
        height: 40
        color: "#0969da"

        anchors {
            left: parent.left
            leftMargin: root.gutter + 6
            top: parent.top
        }
    }
}

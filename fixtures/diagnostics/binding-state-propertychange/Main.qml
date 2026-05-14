// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick

Window {
    id: root
    width: 320
    height: 240
    visible: true

    property bool moved: true
    property int targetX: 44

    Item {
        id: scene
        anchors.fill: parent

        Rectangle {
            id: stateBindingTarget
            x: 8
            y: 20
            width: 48
            height: 28
            color: "#1a7f37"
        }

        states: [
            State {
                name: "moved"
                when: root.moved

                PropertyChanges {
                    target: stateBindingTarget
                    x: root.targetX + 8
                }
            }
        ]
    }
}

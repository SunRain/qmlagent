// Copyright (C) 2026 Penk Chen <penkia@gmail.com>
// SPDX-License-Identifier: Apache-2.0

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.qmlmodels

Window {
    id: root

    width: 1180
    height: 760
    visible: true
    title: "QmlAgent Controls smoke"

    property bool buttonClicked: false
    property bool toolButtonClicked: false
    property bool roundButtonClicked: false
    property bool delegateClicked: false
    property int checkedDelegateClicks: 0
    property int menuTriggered: 0
    property int comboActivated: 0
    property bool popupOpened: false
    property bool dialogOpened: false
    property bool drawerOpened: false

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 18

        ColumnLayout {
            spacing: 8
            Layout.preferredWidth: 250

            Label {
                id: controlsStatus
                property bool buttonState: root.buttonClicked
                property bool toolButtonState: root.toolButtonClicked
                property bool roundButtonState: root.roundButtonClicked
                property bool delegateState: root.delegateClicked
                property bool popupState: root.popupOpened
                property bool dialogState: root.dialogOpened
                property bool drawerState: root.drawerOpened
                property int menuCount: root.menuTriggered
                property int comboCount: root.comboActivated
                property int comboIndex: controlsComboBox.currentIndex
                property string comboText: controlsComboBox.currentText
                property bool toolTipHovered: controlsToolTipButton.hovered
                property real rangeFirstValue: controlsRangeSlider.first.value
                property real rangeSecondValue: controlsRangeSlider.second.value
                property real dialValue: controlsDial.value
                text: "button=%1 tool=%2 round=%3 delegate=%4 menu=%5"
                        .arg(root.buttonClicked)
                        .arg(root.toolButtonClicked)
                        .arg(root.roundButtonClicked)
                        .arg(root.delegateClicked)
                        .arg(root.menuTriggered)
            }

            Button {
                id: controlsButton
                text: "Button"
                onClicked: root.buttonClicked = true
            }

            ToolButton {
                id: controlsToolButton
                text: "ToolButton"
                onClicked: root.toolButtonClicked = true
            }

            RoundButton {
                id: controlsRoundButton
                text: "R"
                onClicked: root.roundButtonClicked = true
            }

            DelayButton {
                id: controlsDelayButton
                text: "DelayButton"
                delay: 60
                Layout.preferredWidth: 170
            }

            CheckBox {
                id: controlsCheckBox
                text: "CheckBox"
            }

            RadioButton {
                id: controlsRadioButton
                text: "RadioButton"
            }

            Switch {
                id: controlsSwitch
                text: "Switch"
            }

            Frame {
                id: controlsFrame
                Layout.preferredWidth: 210
                Layout.preferredHeight: 44

                Label {
                    anchors.centerIn: parent
                    text: "Frame"
                }
            }

            GroupBox {
                id: controlsGroupBox
                title: "GroupBox"
                Layout.preferredWidth: 210
                Layout.preferredHeight: 70

                Label {
                    text: "Grouped"
                }
            }

            ToolBar {
                id: controlsToolBar
                Layout.preferredWidth: 220

                RowLayout {
                    anchors.fill: parent

                    ToolButton {
                        id: controlsToolBarButton
                        text: "TB"
                    }

                    ToolSeparator {
                        id: controlsToolSeparator
                    }
                }
            }
        }

        ColumnLayout {
            spacing: 8
            Layout.preferredWidth: 250

            Slider {
                id: controlsSlider
                from: 0
                to: 10
                value: 4
                Layout.preferredWidth: 220
            }

            RangeSlider {
                id: controlsRangeSlider
                from: 0
                to: 10
                first.value: 2
                second.value: 8
                Layout.preferredWidth: 220
            }

            Dial {
                id: controlsDial
                from: 0
                to: 10
                value: 3
                Layout.preferredWidth: 72
                Layout.preferredHeight: 72
            }

            SpinBox {
                id: controlsSpinBox
                from: 0
                to: 10
                value: 2
                editable: true
                Layout.preferredWidth: 140
            }

            DoubleSpinBox {
                id: controlsDoubleSpinBox
                from: 0
                to: 100
                value: 25
                editable: true
                Layout.preferredWidth: 140
            }

            TextField {
                id: controlsTextField
                text: "editable"
                Layout.preferredWidth: 220
            }

            SearchField {
                id: controlsSearchField
                text: "query"
                Layout.preferredWidth: 220
            }

            TextArea {
                id: controlsTextArea
                text: "notes"
                Layout.preferredWidth: 220
                Layout.preferredHeight: 54
            }

            ComboBox {
                id: controlsComboBox
                model: [ "One", "Two", "Three" ]
                onActivated: root.comboActivated += 1
                Layout.preferredWidth: 220
            }

            Tumbler {
                id: controlsTumbler
                model: [ "A", "B", "C", "D" ]
                visibleItemCount: 3
                Layout.preferredWidth: 90
                Layout.preferredHeight: 84
            }
        }

        ColumnLayout {
            spacing: 8
            Layout.preferredWidth: 250

            CheckDelegate {
                id: controlsCheckDelegate
                text: "CheckDelegate"
                onClicked: root.checkedDelegateClicks += 1
                Layout.preferredWidth: 220
            }

            ItemDelegate {
                id: controlsItemDelegate
                text: "ItemDelegate"
                onClicked: root.delegateClicked = true
                Layout.preferredWidth: 220
            }

            RadioDelegate {
                id: controlsRadioDelegate
                text: "RadioDelegate"
                Layout.preferredWidth: 220
            }

            SwitchDelegate {
                id: controlsSwitchDelegate
                text: "SwitchDelegate"
                Layout.preferredWidth: 220
            }

            SwipeDelegate {
                id: controlsSwipeDelegate
                text: "SwipeDelegate"
                property real swipePosition: swipe.position
                property bool swipeComplete: swipe.complete
                Layout.preferredWidth: 220

                swipe.right: Rectangle {
                    id: controlsSwipeDelegateRightAction
                    width: parent.width
                    height: parent.height
                    anchors.right: parent.right
                    color: SwipeDelegate.pressed ? "#8f1d1d" : "#b3261e"

                    Label {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        color: "white"
                        text: "Archive"
                    }
                }
            }

            PageIndicator {
                id: controlsPageIndicator
                count: 3
                currentIndex: 1
            }

            ProgressBar {
                id: controlsProgressBar
                value: 0.6
                Layout.preferredWidth: 220
            }

            BusyIndicator {
                id: controlsBusyIndicator
                running: true
            }

            ScrollView {
                id: controlsScrollView
                Layout.preferredWidth: 220
                Layout.preferredHeight: 66

                TextArea {
                    id: controlsScrollTextArea
                    text: "Line 1\nLine 2\nLine 3\nLine 4\nLine 5"
                    readOnly: true
                }
            }

            ListView {
                id: controlsFlickableListView
                clip: true
                model: 420
                boundsBehavior: Flickable.StopAtBounds
                Layout.preferredWidth: 220
                Layout.preferredHeight: 104

                delegate: ItemDelegate {
                    id: controlsFlickableListDelegate
                    required property int index
                    width: ListView.view.width
                    height: 28
                    text: "Scrollable row " + (index + 1)
                }
            }

            ScrollBar {
                id: controlsScrollBar
                orientation: Qt.Horizontal
                size: 0.35
                position: 0.2
                Layout.preferredWidth: 180
            }

            ScrollIndicator {
                id: controlsScrollIndicator
                orientation: Qt.Horizontal
                size: 0.35
                position: 0.2
                Layout.preferredWidth: 180
            }
        }

        ColumnLayout {
            spacing: 8
            Layout.preferredWidth: 310

            TabBar {
                id: controlsTabBar
                Layout.preferredWidth: 260

                TabButton {
                    id: controlsTabButton
                    text: "Tab A"
                }

                TabButton {
                    id: controlsSecondTabButton
                    text: "Tab B"
                }
            }

            SwipeView {
                id: controlsSwipeView
                currentIndex: controlsTabBar.currentIndex
                Layout.preferredWidth: 260
                Layout.preferredHeight: 54

                Page {
                    id: controlsPage
                    title: "Page A"

                    Label {
                        anchors.centerIn: parent
                        text: "A"
                    }
                }

                Page {
                    id: controlsSecondPage
                    title: "Page B"
                }
            }

            StackView {
                id: controlsStackView
                initialItem: Label {
                    id: controlsStackLabel
                    text: "Stack"
                }
                Layout.preferredWidth: 260
                Layout.preferredHeight: 44
            }

            SplitView {
                id: controlsSplitView
                orientation: Qt.Horizontal
                Layout.preferredWidth: 260
                Layout.preferredHeight: 44

                Pane {
                    id: controlsSplitLeftPane
                    SplitView.preferredWidth: 110
                }

                Pane {
                    id: controlsSplitRightPane
                    SplitView.preferredWidth: 110
                }
            }

            HorizontalHeaderView {
                id: controlsHorizontalHeaderView
                model: [ "A", "B", "C" ]
                Layout.preferredWidth: 260
                Layout.preferredHeight: 28
            }

            VerticalHeaderView {
                id: controlsVerticalHeaderView
                model: [ "A", "B", "C" ]
                Layout.preferredWidth: 80
                Layout.preferredHeight: 86
            }

            TableView {
                id: controlsTableView
                model: TableModel {
                    TableModelColumn { display: "name" }
                    rows: [
                        { "name": "Alpha" },
                        { "name": "Beta" }
                    ]
                }
                clip: true
                Layout.preferredWidth: 260
                Layout.preferredHeight: 64

                delegate: TableViewDelegate {
                    id: controlsTableViewDelegate
                    implicitWidth: 120
                    implicitHeight: 28
                    text: display
                }
            }

            TreeView {
                id: controlsTreeView
                model: TableModel {
                    TableModelColumn { display: "name" }
                    rows: [
                        { "name": "Root" },
                        { "name": "Leaf" }
                    ]
                }
                clip: true
                Layout.preferredWidth: 260
                Layout.preferredHeight: 64

                delegate: TreeViewDelegate {
                    id: controlsTreeViewDelegate
                    implicitWidth: 120
                    implicitHeight: 28
                    text: display
                }
            }

            GridLayout {
                columns: 2
                Layout.fillWidth: true

                Button {
                    id: controlsOpenPopupButton
                    text: "Popup"
                    Layout.fillWidth: true
                    onClicked: {
                        root.popupOpened = true
                        controlsPopup.open()
                    }
                }

                Button {
                    id: controlsOpenDialogButton
                    text: "Dialog"
                    Layout.fillWidth: true
                    onClicked: {
                        root.dialogOpened = true
                        controlsDialog.open()
                    }
                }

                Button {
                    id: controlsOpenDrawerButton
                    text: "Drawer"
                    Layout.fillWidth: true
                    onClicked: {
                        root.drawerOpened = true
                        controlsDrawer.open()
                    }
                }

                Button {
                    id: controlsOpenMenuButton
                    text: "Menu"
                    Layout.fillWidth: true
                    onClicked: controlsStandaloneMenu.open()
                }
            }

            MenuBar {
                id: controlsMenuBar
                Layout.preferredWidth: 260

                MenuBarItem {
                    id: controlsMenuBarItem
                    text: "Menu"
                    menu: Menu {
                        id: controlsMenu

                        MenuItem {
                            id: controlsMenuItem
                            text: "MenuItem"
                            onTriggered: root.menuTriggered += 1
                        }

                        MenuSeparator {
                            id: controlsMenuSeparator
                        }
                    }
                }
            }

            Button {
                id: controlsToolTipButton
                text: "ToolTip"
                hoverEnabled: true
                Layout.preferredWidth: 120
            }
        }
    }

    Popup {
        id: controlsPopup
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        x: 360
        y: 90
        width: 180
        height: 90

        contentItem: Button {
            id: controlsPopupCloseButton
            text: "Close Popup"
            onClicked: controlsPopup.close()
        }
    }

    Dialog {
        id: controlsDialog
        modal: true
        closePolicy: Popup.NoAutoClose
        x: 360
        y: 210
        width: 200
        height: 110
        standardButtons: Dialog.NoButton

        contentItem: Button {
            id: controlsDialogConfirmButton
            text: "Confirm"
            onClicked: controlsDialog.close()
        }
    }

    Drawer {
        id: controlsDrawer
        edge: Qt.LeftEdge
        width: 180
        height: root.height
        enter: Transition {}
        exit: Transition {}

        Button {
            id: controlsDrawerCloseButton
            anchors.centerIn: parent
            text: "Close Drawer"
            onClicked: controlsDrawer.close()
        }
    }

    Menu {
        id: controlsStandaloneMenu
        x: 820
        y: 118

        MenuItem {
            id: controlsStandaloneMenuItem
            text: "Standalone MenuItem"
            onTriggered: root.menuTriggered += 1
        }
    }

    ToolTip {
        id: controlsToolTip
        parent: controlsToolTipButton
        text: "ToolTip"
        delay: 0
        timeout: 60000
        visible: controlsToolTipButton.hovered

        contentItem: Label {
            id: controlsToolTipLabel
            text: controlsToolTip.text
        }
    }
}

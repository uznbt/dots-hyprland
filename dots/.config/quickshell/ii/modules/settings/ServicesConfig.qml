import QtQuick
import QtQuick.Layouts
import qs.services
import qs.modules.common
import qs.modules.common.widgets

ContentPage {
    forceWidth: true

    ContentSection {
        icon: "neurology"
        title: Translation.tr("AI")

        MaterialTextArea {
            Layout.fillWidth: true
            placeholderText: Translation.tr("System prompt")
            text: Config.options.ai.systemPrompt
            wrapMode: TextEdit.Wrap
            onTextChanged: {
                Qt.callLater(() => {
                    Config.options.ai.systemPrompt = text;
                });
            }
        }

        ContentSubsection {
            title: Translation.tr("AI policy")

            ConfigSelectionArray {
                currentValue: Config.options.policies.ai
                onSelected: newValue => {
                    Config.options.policies.ai = newValue;
                }
                options: [
                    {
                        displayName: Translation.tr("No"),
                        icon: "close",
                        value: 0
                    },
                    {
                        displayName: Translation.tr("Yes"),
                        icon: "check",
                        value: 1
                    },
                    {
                        displayName: Translation.tr("Local only"),
                        icon: "sync_saved_locally",
                        value: 2
                    },
                ]
            }

            StyledText {
                Layout.fillWidth: true
                text: Translation.tr("Online models, including OpenCode free models, require the \"Yes\" policy. \"Local only\" hides them.")
                color: Appearance.colors.colSubtext
                wrapMode: Text.Wrap
                font.pixelSize: Appearance.font.pixelSize.small
            }
        }

        ContentSubsection {
            title: Translation.tr("Model")

            StyledComboBox {
                id: aiModelSelector
                buttonIcon: "neurology"
                textRole: "displayName"
                model: Ai.modelList.map(modelId => {
                    const model = Ai.models[modelId];
                    return {
                        displayName: (model?.api_format === "opencode" ? "OpenCode · " : "") + (model?.name ?? modelId),
                        value: modelId
                    };
                })
                currentIndex: {
                    const index = model.findIndex(item => item.value === Ai.currentModelId);
                    return index !== -1 ? index : 0;
                }
                onActivated: index => {
                    Ai.setModel(model[index].value);
                }
            }

            ConfigSlider {
                buttonIcon: "device_thermostat"
                text: Translation.tr("Temperature")
                value: Ai.temperature
                from: 0
                to: 2
                onValueChanged: {
                    Persistent.states.ai.temperature = value;
                    Ai.temperature = value;
                }
            }

            StyledText {
                Layout.fillWidth: true
                text: Translation.tr("OpenCode free models are detected from the installed opencode CLI. No API key is needed. Data handling depends on the selected model.")
                color: Appearance.colors.colSubtext
                wrapMode: Text.Wrap
                font.pixelSize: Appearance.font.pixelSize.small
            }

            RippleButtonWithIcon {
                materialIcon: "refresh"
                mainText: Translation.tr("Refresh OpenCode models")
                onClicked: Ai.refreshOpencodeModels()
            }
        }

        ContentSubsection {
            title: Translation.tr("OpenCode agent")

            ConfigSelectionArray {
                currentValue: Config.options.ai.opencodeAgent
                onSelected: newValue => {
                    Config.options.ai.opencodeAgent = newValue;
                }
                options: [
                    {
                        displayName: Translation.tr("Build (can edit)"),
                        icon: "build",
                        value: "build"
                    },
                    {
                        displayName: Translation.tr("Plan (read-only)"),
                        icon: "visibility",
                        value: "plan"
                    },
                ]
            }

            StyledText {
                Layout.fillWidth: true
                text: Translation.tr("Build can read and edit files. Plan only reads. This applies to the next OpenCode message.")
                color: Appearance.colors.colSubtext
                wrapMode: Text.Wrap
                font.pixelSize: Appearance.font.pixelSize.small
            }
        }

        ContentSubsection {
            title: Translation.tr("OpenCode working folder")

            StyledText {
                Layout.fillWidth: true
                text: Translation.tr("Folder that OpenCode models can access. Leave empty to use a temporary folder. The selected agent decides whether files can be edited. Changing it starts a new session.")
                color: Appearance.colors.colSubtext
                wrapMode: Text.Wrap
                font.pixelSize: Appearance.font.pixelSize.small
            }

            MaterialTextField {
                Layout.fillWidth: true
                placeholderText: Translation.tr("Example: ~/Projects/my-app")
                text: Config.options.ai.opencodeWorkDir
                onTextChanged: {
                    Qt.callLater(() => {
                        Config.options.ai.opencodeWorkDir = text;
                    });
                }
            }

            StyledText {
                Layout.fillWidth: true
                text: Translation.tr("Manage sessions from the AI widget: /sessions to list, /resume to continue, /new to start fresh, /folder to change this folder.")
                color: Appearance.colors.colSubtext
                wrapMode: Text.Wrap
                font.pixelSize: Appearance.font.pixelSize.small
            }
        }
    }

    ContentSection {
        icon: "music_cast"
        title: Translation.tr("Music Recognition")

        ConfigSpinBox {
            icon: "timer_off"
            text: Translation.tr("Total duration timeout (s)")
            value: Config.options.musicRecognition.timeout
            from: 10
            to: 100
            stepSize: 2
            onValueChanged: {
                Config.options.musicRecognition.timeout = value;
            }
        }
        ConfigSpinBox {
            icon: "av_timer"
            text: Translation.tr("Polling interval (s)")
            value: Config.options.musicRecognition.interval
            from: 2
            to: 10
            stepSize: 1
            onValueChanged: {
                Config.options.musicRecognition.interval = value;
            }
        }
    }

    ContentSection {
        icon: "cell_tower"
        title: Translation.tr("Networking")

        MaterialTextArea {
            Layout.fillWidth: true
            placeholderText: Translation.tr("User agent (for services that require it)")
            text: Config.options.networking.userAgent
            wrapMode: TextEdit.Wrap
            onTextChanged: {
                Config.options.networking.userAgent = text;
            }
        }
    }

    ContentSection {
        icon: "memory"
        title: Translation.tr("Resources")

        ConfigSpinBox {
            icon: "av_timer"
            text: Translation.tr("Polling interval (ms)")
            value: Config.options.resources.updateInterval
            from: 100
            to: 10000
            stepSize: 100
            onValueChanged: {
                Config.options.resources.updateInterval = value;
            }
        }
        
    }

    ContentSection {
        icon: "file_open"
        title: Translation.tr("Save paths")

        MaterialTextArea {
            Layout.fillWidth: true
            placeholderText: Translation.tr("Video Recording Path")
            text: Config.options.screenRecord.savePath
            wrapMode: TextEdit.Wrap
            onTextChanged: {
                Config.options.screenRecord.savePath = text;
            }
        }
        
        MaterialTextArea {
            Layout.fillWidth: true
            placeholderText: Translation.tr("Screenshot Path (leave empty to just copy)")
            text: Config.options.screenSnip.savePath
            wrapMode: TextEdit.Wrap
            onTextChanged: {
                Config.options.screenSnip.savePath = text;
            }
        }
    }

    ContentSection {
        icon: "search"
        title: Translation.tr("Search")

        ConfigSwitch {
            text: Translation.tr("Use Levenshtein distance-based algorithm instead of fuzzy")
            checked: Config.options.search.sloppy
            onCheckedChanged: {
                Config.options.search.sloppy = checked;
            }
            StyledToolTip {
                text: Translation.tr("Could be better if you make a ton of typos,\nbut results can be weird and might not work with acronyms\n(e.g. \"GIMP\" might not give you the paint program)")
            }
        }

        ContentSubsection {
            title: Translation.tr("Prefixes")
            ConfigRow {
                uniform: true
                MaterialTextArea {
                    Layout.fillWidth: true
                    placeholderText: Translation.tr("Action")
                    text: Config.options.search.prefix.action
                    wrapMode: TextEdit.Wrap
                    onTextChanged: {
                        Config.options.search.prefix.action = text;
                    }
                }
                MaterialTextArea {
                    Layout.fillWidth: true
                    placeholderText: Translation.tr("Clipboard")
                    text: Config.options.search.prefix.clipboard
                    wrapMode: TextEdit.Wrap
                    onTextChanged: {
                        Config.options.search.prefix.clipboard = text;
                    }
                }
                MaterialTextArea {
                    Layout.fillWidth: true
                    placeholderText: Translation.tr("Emojis")
                    text: Config.options.search.prefix.emojis
                    wrapMode: TextEdit.Wrap
                    onTextChanged: {
                        Config.options.search.prefix.emojis = text;
                    }
                }
            }

            ConfigRow {
                uniform: true
                MaterialTextArea {
                    Layout.fillWidth: true
                    placeholderText: Translation.tr("Math")
                    text: Config.options.search.prefix.math
                    wrapMode: TextEdit.Wrap
                    onTextChanged: {
                        Config.options.search.prefix.math = text;
                    }
                }
                MaterialTextArea {
                    Layout.fillWidth: true
                    placeholderText: Translation.tr("Shell command")
                    text: Config.options.search.prefix.shellCommand
                    wrapMode: TextEdit.Wrap
                    onTextChanged: {
                        Config.options.search.prefix.shellCommand = text;
                    }
                }
                MaterialTextArea {
                    Layout.fillWidth: true
                    placeholderText: Translation.tr("Web search")
                    text: Config.options.search.prefix.webSearch
                    wrapMode: TextEdit.Wrap
                    onTextChanged: {
                        Config.options.search.prefix.webSearch = text;
                    }
                }
            }
        }
        ContentSubsection {
            title: Translation.tr("Web search")
            MaterialTextArea {
                Layout.fillWidth: true
                placeholderText: Translation.tr("Base URL")
                text: Config.options.search.engineBaseUrl
                wrapMode: TextEdit.Wrap
                onTextChanged: {
                    Config.options.search.engineBaseUrl = text;
                }
            }
        }
    }

    // There's no update indicator in ii for now so we shouldn't show this yet
    // ContentSection {
    //     icon: "deployed_code_update"
    //     title: Translation.tr("System updates (Arch only)")

    //     ConfigSwitch {
    //         text: Translation.tr("Enable update checks")
    //         checked: Config.options.updates.enableCheck
    //         onCheckedChanged: {
    //             Config.options.updates.enableCheck = checked;
    //         }
    //     }

    //     ConfigSpinBox {
    //         icon: "av_timer"
    //         text: Translation.tr("Check interval (mins)")
    //         value: Config.options.updates.checkInterval
    //         from: 60
    //         to: 1440
    //         stepSize: 60
    //         onValueChanged: {
    //             Config.options.updates.checkInterval = value;
    //         }
    //     }
    // }

    ContentSection {
        icon: "weather_mix"
        title: Translation.tr("Weather")
        ConfigRow {
            ConfigSwitch {
                buttonIcon: "assistant_navigation"
                text: Translation.tr("Enable GPS based location")
                checked: Config.options.bar.weather.enableGPS
                onCheckedChanged: {
                    Config.options.bar.weather.enableGPS = checked;
                }
            }
            ConfigSwitch {
                buttonIcon: "thermometer"
                text: Translation.tr("Fahrenheit unit")
                checked: Config.options.bar.weather.useUSCS
                onCheckedChanged: {
                    Config.options.bar.weather.useUSCS = checked;
                }
                StyledToolTip {
                    text: Translation.tr("It may take a few seconds to update")
                }
            }
        }
        
        MaterialTextArea {
            Layout.fillWidth: true
            placeholderText: Translation.tr("City name")
            text: Config.options.bar.weather.city
            wrapMode: TextEdit.Wrap
            onTextChanged: {
                Config.options.bar.weather.city = text;
            }
        }
        ConfigSpinBox {
            icon: "av_timer"
            text: Translation.tr("Polling interval (m)")
            value: Config.options.bar.weather.fetchInterval
            from: 5
            to: 50
            stepSize: 5
            onValueChanged: {
                Config.options.bar.weather.fetchInterval = value;
            }
        }
    }
}

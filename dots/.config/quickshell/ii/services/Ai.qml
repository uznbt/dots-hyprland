pragma Singleton
pragma ComponentBehavior: Bound

import qs.modules.common.functions as CF
import qs.modules.common
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick
import qs.services.ai

/**
 * Basic service to handle LLM chats. Supports Google's and OpenAI's API formats.
 * Supports Gemini and OpenAI models.
 * Limitations:
 * - For now functions only work with Gemini API format
 */
Singleton {
    id: root

    property Component aiMessageComponent: AiMessageData {}
    property Component aiModelComponent: AiModel {}
    property Component geminiApiStrategy: GeminiApiStrategy {}
    property Component openaiApiStrategy: OpenAiApiStrategy {}
    property Component mistralApiStrategy: MistralApiStrategy {}
    property Component opencodeApiStrategy: OpencodeApiStrategy {}
    readonly property string interfaceRole: "interface"
    readonly property string apiKeyEnvVarName: "API_KEY"

    signal responseFinished()

    property string systemPrompt: {
        let prompt = Config.options?.ai?.systemPrompt ?? "";
        for (let key in root.promptSubstitutions) {
            // prompt = prompt.replaceAll(key, root.promptSubstitutions[key]);
            // QML/JS doesn't support replaceAll, so use split/join
            prompt = prompt.split(key).join(root.promptSubstitutions[key]);
        }
        return prompt;
    }
    // property var messages: []
    property var messageIDs: []
    property var messageByID: ({})
    readonly property var apiKeys: KeyringStorage.keyringData?.apiKeys ?? {}
    readonly property var apiKeysLoaded: KeyringStorage.loaded
    readonly property bool currentModelHasApiKey: {
        const model = (models ?? {})[currentModelId];
        if (!model || !model.requires_key) return true;
        if (!apiKeysLoaded) return false;
        const key = apiKeys[model.key_id];
        return (key?.length > 0);
    }
    property var postResponseHook
    property real temperature: Persistent.states?.ai?.temperature ?? 0.5
    property QtObject tokenCount: QtObject {
        property int input: -1
        property int output: -1
        property int total: -1
    }

    function idForMessage(message) {
        // Generate a unique ID using timestamp and random value
        return Date.now().toString(36) + Math.random().toString(36).substr(2, 8);
    }

    function safeModelName(modelName) {
        return modelName.replace(/:/g, "_").replace(/ /g, "-").replace(/\//g, "-")
    }

    property list<var> defaultPrompts: []
    property list<var> userPrompts: []
    property list<var> promptFiles: [...defaultPrompts, ...userPrompts]
    property list<var> savedChats: []

    property var promptSubstitutions: {
        "{DISTRO}": SystemInfo.distroName,
        "{DATETIME}": `${DateTime.time}, ${DateTime.collapsedCalendarFormat}`,
        "{WINDOWCLASS}": ToplevelManager.activeToplevel?.appId ?? "Unknown",
        "{DE}": `${SystemInfo.desktopEnvironment} (${SystemInfo.windowingSystem})` 
    }

    // Gemini: https://ai.google.dev/gemini-api/docs/function-calling
    // OpenAI: https://platform.openai.com/docs/guides/function-calling
    property string currentTool: {
        const modelTools = (root.tools ?? {})[(models ?? {})[currentModelId]?.api_format] ?? {};
        const configuredTool = Config?.options.ai.tool ?? "search";
        if (configuredTool in modelTools) return configuredTool;
        return Object.keys(modelTools)[0] ?? "none";
    }
    property var tools: {
        "gemini": {
            "functions": [{"functionDeclarations": [
                {
                    "name": "switch_to_search_mode",
                    "description": "Search the web",
                },
                {
                    "name": "get_shell_config",
                    "description": "Get the desktop shell config file contents",
                },
                {
                    "name": "set_shell_config",
                    "description": "Set a field in the desktop graphical shell config file. Must only be used after `get_shell_config`.",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "key": {
                                "type": "string",
                                "description": "The key to set, e.g. `bar.borderless`. MUST NOT BE GUESSED, use `get_shell_config` to see what keys are available before setting.",
                            },
                            "value": {
                                "type": "string",
                                "description": "The value to set, e.g. `true`"
                            }
                        },
                        "required": ["key", "value"]
                    }
                },
                {
                    "name": "run_shell_command",
                    "description": "Run a shell command in bash and get its output. Use this only for quick commands that don't require user interaction. For commands that require interaction, ask the user to run manually instead.",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "command": {
                                "type": "string",
                                "description": "The bash command to run",
                            },
                        },
                        "required": ["command"]
                    }
                },
            ]}],
            "search": [{
                "google_search": {}
            }],
            "none": []
        },
        "openai": {
            "functions": [
                {
                    "type": "function",
                    "function": {
                        "name": "get_shell_config",
                        "description": "Get the desktop shell config file contents",
                        "parameters": {}
                    },
                },
                {
                    "type": "function",
                    "function": {
                        "name": "set_shell_config",
                        "description": "Set a field in the desktop graphical shell config file. Must only be used after `get_shell_config`.",
                        "parameters": {
                            "type": "object",
                            "properties": {
                                "key": {
                                    "type": "string",
                                    "description": "The key to set, e.g. `bar.borderless`. MUST NOT BE GUESSED, use `get_shell_config` to see what keys are available before setting.",
                                },
                                "value": {
                                    "type": "string",
                                    "description": "The value to set, e.g. `true`"
                                }
                            },
                            "required": ["key", "value"]
                        }
                    }
                },
                {
                    "type": "function",
                    "function": {
                        "name": "run_shell_command",
                        "description": "Run a shell command in bash and get its output. Use this only for quick commands that don't require user interaction. For commands that require interaction, ask the user to run manually instead.",
                        "parameters": {
                            "type": "object",
                            "properties": {
                                "command": {
                                    "type": "string",
                                    "description": "The bash command to run",
                                },
                            },
                            "required": ["command"]
                        }
                    },
                },
            ],
            "search": [],
            "none": [],
        },
        "mistral": {
            "functions": [
                {
                    "type": "function",
                    "function": {
                        "name": "get_shell_config",
                        "description": "Get the desktop shell config file contents",
                        "parameters": {}
                    },
                },
                {
                    "type": "function",
                    "function": {
                        "name": "set_shell_config",
                        "description": "Set a field in the desktop graphical shell config file. Must only be used after `get_shell_config`.",
                        "parameters": {
                            "type": "object",
                            "properties": {
                                "key": {
                                    "type": "string",
                                    "description": "The key to set, e.g. `bar.borderless`. MUST NOT BE GUESSED, use `get_shell_config` to see what keys are available before setting.",
                                },
                                "value": {
                                    "type": "string",
                                    "description": "The value to set, e.g. `true`"
                                }
                            },
                            "required": ["key", "value"]
                        }
                    }
                },
                {
                    "type": "function",
                    "function": {
                        "name": "run_shell_command",
                        "description": "Run a shell command in bash and get its output. Use this only for quick commands that don't require user interaction. For commands that require interaction, ask the user to run manually instead.",
                        "parameters": {
                            "type": "object",
                            "properties": {
                                "command": {
                                    "type": "string",
                                    "description": "The bash command to run",
                                },
                            },
                            "required": ["command"]
                        }
                    },
                },
            ],
            "search": [],
            "none": [],
        },
        "opencode": {
            // OpenCode free models are driven through the CLI, which manages
            // its own tool loop. The sidebar only sends plain chat messages.
            "none": [],
        },
    }
    property list<var> availableTools: Object.keys((root.tools ?? {})[(models ?? {})[currentModelId]?.api_format] ?? {})
    property var toolDescriptions: {
        "functions": Translation.tr("Commands, edit configs, search.\nTakes an extra turn to switch to search mode if that's needed"),
        "search": Translation.tr("Gives the model search capabilities (immediately)"),
        "none": Translation.tr("Disable tools")
    }

    // Model properties:
    // - name: Name of the model
    // - icon: Icon name of the model
    // - description: Description of the model
    // - endpoint: Endpoint of the model
    // - model: Model name of the model
    // - requires_key: Whether the model requires an API key
    // - key_id: The identifier of the API key. Use the same identifier for models that can be accessed with the same key.
    // - key_get_link: Link to get an API key
    // - key_get_description: Description of pricing and how to get an API key
    // - api_format: The API format of the model. Can be "openai", "gemini", "mistral", or "opencode". Default is "openai".
    // - extraParams: Extra parameters to be passed to the model. This is a JSON object.
    property var baseModels: {
        "gemini-2.5-flash": aiModelComponent.createObject(this, {
            "name": "Gemini 2.5 Flash",
            "icon": "google-gemini-symbolic",
            "description": Translation.tr("Online | Google's model\nNewer model that's slower than its predecessor but should deliver higher quality answers"),
            "homepage": "https://aistudio.google.com",
            "endpoint": "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:streamGenerateContent",
            "model": "gemini-2.5-flash",
            "requires_key": true,
            "key_id": "gemini",
            "key_get_link": "https://aistudio.google.com/app/apikey",
            "key_get_description": Translation.tr("**Pricing**: free. Data used for training.\n\n**Instructions**: Log into Google account, allow AI Studio to create Google Cloud project or whatever it asks, go back and click Get API key"),
            "api_format": "gemini",
        }),
        "gemini-3-flash": aiModelComponent.createObject(this, {
            "name": "Gemini 3 Flash",
            "icon": "google-gemini-symbolic",
            "description": Translation.tr("Online | Google's model\nPro-level intelligence at the speed and pricing of Flash."),
            "homepage": "https://aistudio.google.com",
            "endpoint": "https://generativelanguage.googleapis.com/v1beta/models/gemini-3-flash-preview:streamGenerateContent",
            "model": "gemini-3-flash-preview",
            "requires_key": true,
            "key_id": "gemini",
            "key_get_link": "https://aistudio.google.com/app/apikey",
            "key_get_description": Translation.tr("**Pricing**: free. Data used for training.\n\n**Instructions**: Log into Google account, allow AI Studio to create Google Cloud project or whatever it asks, go back and click Get API key"),
            "api_format": "gemini",
        }),
        "mistral-medium-3": aiModelComponent.createObject(this, {
            "name": "Mistral Medium 3",
            "icon": "mistral-symbolic",
            "description": Translation.tr("Online | %1's model | Delivers fast, responsive and well-formatted answers. Disadvantages: not very eager to do stuff; might make up unknown function calls").arg("Mistral"),
            "homepage": "https://mistral.ai/news/mistral-medium-3",
            "endpoint": "https://api.mistral.ai/v1/chat/completions",
            "model": "mistral-medium-2505",
            "requires_key": true,
            "key_id": "mistral",
            "key_get_link": "https://console.mistral.ai/api-keys",
            "key_get_description": Translation.tr("**Instructions**: Log into Mistral account, go to Keys on the sidebar, click Create new key"),
            "api_format": "mistral",
        }),
    }
    property var dynamicModels: ({})
    property var models: {
        const allModels = Object.assign({}, root.baseModels, root.dynamicModels);
        if (Config.options.policies.ai !== 2) return allModels;

        // "Local only" still allows local servers such as Ollama.
        const localModels = {};
        for (const key in allModels) {
            if (allModels[key].endpoint?.includes("localhost")) localModels[key] = allModels[key];
        }
        return localModels;
    }
    property var modelList: Object.keys(root.models)
    property var currentModelId: Persistent.states?.ai?.model || modelList[0]

    property var apiStrategies: {
        "openai": openaiApiStrategy.createObject(this),
        "gemini": geminiApiStrategy.createObject(this),
        "mistral": mistralApiStrategy.createObject(this),
        "opencode": opencodeApiStrategy.createObject(this),
    }
    property ApiStrategy currentApiStrategy: (apiStrategies ?? {})[(models ?? {})[currentModelId]?.api_format || "openai"] ?? null

    property var opencodeModelIds: []
    property bool opencodeModelsLoaded: false
    readonly property var fallbackOpencodeModelIds: [
        "beam-free",
        "big-pickle",
        "deepseek-v4.1-flash-free",
        "glm-5.3-flash-free",
        "longcat-2.5-preview-free",
        "mimo-v2.6-flash-free",
        "minimax-m3.1-flash-free",
        "nemotron-3-ultra-free",
        "nemotron-3.5-lightning-free",
        "nemotron-3.5-super-vl-free",
    ]
    property string opencodeWorkDir: "/tmp/quickshell/ai/opencode-work"
    property string opencodeSessionId: ""
    property var opencodeSessions: []
    property bool opencodeSessionsRequested: false

    function resolveUserPath(path) {
        let resolved = CF.FileUtils.trimFileProtocol((path ?? "").trim());
        if (resolved.length === 0) return resolved;
        const home = CF.FileUtils.trimFileProtocol(Directories.home);
        if (resolved === "~") resolved = home;
        else if (resolved.startsWith("~/")) resolved = `${home}/${resolved.substring(2)}`;
        else if (!resolved.startsWith("/")) resolved = `${home}/${resolved}`;
        return resolved;
    }

    function resolveOpencodeWorkDir() {
        const configured = (Config?.options?.ai?.opencodeWorkDir ?? "").trim();
        return configured.length === 0 ? root.opencodeWorkDir : root.resolveUserPath(configured);
    }

    function resolveOpencodeAgent() {
        const agent = (Config?.options?.ai?.opencodeAgent ?? "build").trim().toLowerCase();
        return agent === "plan" ? "plan" : "build";
    }

    function prettyOpencodeModelName(modelId) {
        const specialNames = {
            "glm": "GLM",
            "mimo": "MiMo",
            "minimax": "MiniMax",
            "deepseek": "DeepSeek",
            "longcat": "LongCat",
            "nemotron": "Nemotron",
            "vl": "VL",
            "ai": "AI",
        };
        return modelId.split("-").map(part => {
            if (specialNames[part]) return specialNames[part];
            if (part.length === 0) return part;
            return part.charAt(0).toUpperCase() + part.slice(1);
        }).join(" ");
    }

    function addOpencodeModels(modelIds) {
        const ids = [...new Set((modelIds ?? [])
            .map(id => id.trim())
            .filter(id => id.length > 0))];
        const nextModels = Object.assign({}, root.dynamicModels);
        const keys = [];

        ids.forEach(id => {
            const key = root.safeModelName(`opencode/${id}`);
            keys.push(key);
            if (nextModels[key]) return;
            nextModels[key] = root.aiModelComponent.createObject(root, {
                "name": root.prettyOpencodeModelName(id),
                "icon": id.includes("deepseek") ? "deepseek-symbolic" : "spark-symbolic",
                "description": Translation.tr("Free | OpenCode's model\nUses the local opencode CLI. No API key needed."),
                "homepage": "https://opencode.ai/docs/zen",
                "endpoint": "https://opencode.ai/inference/openai/v1/chat/completions",
                "model": `opencode/${id}`,
                "requires_key": false,
                "api_format": "opencode",
            });
        });

        root.dynamicModels = nextModels;
        root.opencodeModelIds = keys;
        root.opencodeModelsLoaded = true;

        // If the persisted model was an OpenCode model, finish its setup now
        // that the model has been registered.
        if (root.modelList.indexOf(root.currentModelId) !== -1) {
            root.setModel(root.currentModelId, false, false);
        }
    }

    function refreshOpencodeModels() {
        root.opencodeModelsLoaded = false;
        getOpencodeModels.running = false;
        getOpencodeModels.running = true;
    }

    function newOpencodeSession() {
        root.clearMessages();
        root.addMessage(Translation.tr("Started a new OpenCode session."), root.interfaceRole);
    }

    function printOpencodeFolder() {
        const configured = (Config?.options?.ai?.opencodeWorkDir ?? "").trim();
        const resolved = root.resolveOpencodeWorkDir();
        const agent = root.resolveOpencodeAgent();
        const source = configured.length > 0
            ? Translation.tr("Set in Settings → Services → AI.")
            : Translation.tr("Using the temporary default folder.");
        root.addMessage(
            `**OpenCode folder**\n\n\`\`\`\n${resolved}\n\`\`\`\n\nAgent: **${agent}**\n${source}\n\nChange with \`/folder PATH\`, or reset with \`/folder default\`.`,
            root.interfaceRole
        );
    }

    function setOpencodeFolder(path) {
        const value = (path ?? "").trim();
        if (value.length === 0) {
            root.printOpencodeFolder();
            return;
        }
        if (value === "default" || value === "reset") {
            Config.options.ai.opencodeWorkDir = "";
            root.clearMessages();
            root.addMessage(Translation.tr("OpenCode folder reset to the temporary default. Started a new session."), root.interfaceRole);
            return;
        }
        const resolved = root.resolveUserPath(value);
        Config.options.ai.opencodeWorkDir = resolved;
        root.clearMessages();
        root.addMessage(`OpenCode folder set to:\n\n\`\`\`\n${resolved}\n\`\`\`\n\nStarted a new session.`, root.interfaceRole);
    }

    function listOpencodeSessions() {
        root.opencodeSessionsRequested = true;
        getOpencodeSessions.running = false;
        getOpencodeSessions.running = true;
    }

    function printOpencodeSessions() {
        const sessions = root.opencodeSessions ?? [];
        if (sessions.length === 0) {
            root.addMessage(
                `No OpenCode sessions found in:\n\n\`\`\`\n${root.resolveOpencodeWorkDir()}\n\`\`\`\n\nSet a folder with \`/folder PATH\`, then try again.`,
                root.interfaceRole
            );
            return;
        }
        const lines = sessions.map((session, index) => {
            const marker = session.id === root.opencodeSessionId ? " ← current" : "";
            const date = new Date(session.updated ?? session.created ?? 0).toLocaleString();
            return `${index + 1}. **${session.title || "(untitled)"}**\n   \`${session.id}\` · ${date}${marker}`;
        });
        root.addMessage(
            `OpenCode sessions in this folder:\n\n${lines.join("\n\n")}\n\nUse \`/resume NUMBER\` or \`/resume SESSION_ID\`.`,
            root.interfaceRole
        );
    }

    function resumeOpencodeSession(selector) {
        const value = (selector ?? "").trim();
        if (value.length === 0) {
            root.listOpencodeSessions();
            root.addMessage(Translation.tr("Usage: `/sessions` then `/resume NUMBER`, or `/resume SESSION_ID`."), root.interfaceRole);
            return;
        }
        let target = value;
        if (/^\d+$/.test(value)) {
            const index = parseInt(value) - 1;
            if (index < 0 || index >= (root.opencodeSessions?.length ?? 0)) {
                root.addMessage(Translation.tr("Session number not found. Run `/sessions` first."), root.interfaceRole);
                return;
            }
            target = root.opencodeSessions[index].id;
        }
        if (!target.startsWith("ses_")) {
            root.addMessage(Translation.tr("That does not look like an OpenCode session ID. Run `/sessions` to see the list."), root.interfaceRole);
            return;
        }
        exportOpencodeSession.sessionId = target;
        exportOpencodeSession.running = false;
        exportOpencodeSession.running = true;
    }

    function importOpencodeSession(exportText) {
        try {
            const data = JSON.parse(exportText);
            const targetSessionId = data.info?.id ?? root.opencodeSessionId;
            const imported = [];
            for (const message of (data.messages ?? [])) {
                if (message.type === "user" && message.text) {
                    let content = String(message.text).trim();
                    const startMarker = "<<<USER_MESSAGE>>>";
                    const endMarker = "<<<END_USER_MESSAGE>>>";
                    const start = content.lastIndexOf(startMarker);
                    const end = start >= 0 ? content.indexOf(endMarker, start + startMarker.length) : -1;
                    if (start >= 0 && end > start) {
                        content = content.substring(start + startMarker.length, end).trim();
                    } else {
                        // Older sessions do not have markers. Extract the
                        // actual message from the composed CLI prompt instead.
                        if (content.includes("## New message")) {
                            const parts = content.split("## New message");
                            content = (parts[parts.length - 1] ?? "").split("## Your next reply")[0].trim();
                        } else if (content.includes("## Conversation so far")) {
                            const parts = content.split("## Conversation so far");
                            const tail = parts[parts.length - 1] ?? "";
                            const userParts = tail.split("### User");
                            content = (userParts[userParts.length - 1] ?? "").split("## Your next reply")[0].trim();
                        }
                    }
                    if (content.length > 0) imported.push({ role: "user", content });
                } else if (message.type === "assistant") {
                    const content = (message.content ?? [])
                        .filter(part => part.type === "text" && part.text)
                        .map(part => part.text)
                        .join("\n")
                        .trim();
                    if (content.length > 0) imported.push({ role: "assistant", content });
                }
            }

            root.clearMessages();
            root.opencodeSessionId = targetSessionId;
            const newIds = [];
            const newById = ({});
            for (const item of imported) {
                const aiMessage = root.aiMessageComponent.createObject(root, {
                    "role": item.role,
                    "content": item.content,
                    "rawContent": item.content,
                    "model": item.role === "assistant" ? root.currentModelId : "",
                    "thinking": false,
                    "done": true,
                });
                const id = root.idForMessage(aiMessage);
                newIds.push(id);
                newById[id] = aiMessage;
            }
            root.messageIDs = newIds;
            root.messageByID = newById;
            root.addMessage(
                `Resumed OpenCode session:\n\n\`${targetSessionId}\`\n\n${imported.length} messages loaded.`,
                root.interfaceRole
            );
            root.saveChat("lastSession");
        } catch (e) {
            console.log("[AI][opencode] Could not import session:", e);
            root.addMessage(Translation.tr("Could not load that OpenCode session. Run `/sessions` and try again."), root.interfaceRole);
        }
    }

    function addUserModels() {
        (Config?.options.ai?.extraModels ?? []).forEach(model => {
            const safeModelName = root.safeModelName(model["model"]);
            root.addModel(safeModelName, model)
        });
    }

    Connections {
        target: Config
        function onReadyChanged() {
            if (!Config.ready) return;
            root.addUserModels()
        }
    }

    Connections {
        target: Config.options.ai
        function onOpencodeWorkDirChanged() {
            // Sessions belong to a project folder. Don't try to continue an
            // old session after the user points OpenCode somewhere else.
            root.opencodeSessionId = "";
        }
    }

    property string requestScriptFilePath: "/tmp/quickshell/ai/request.sh"
    property string pendingFilePath: ""

    Component.onCompleted: {
        setModel(currentModelId, false, false); // Do necessary setup for model
        root.addUserModels() // Config onReadyChanged above might not fire if config is loaded before this service
        root.addOpencodeModels(root.fallbackOpencodeModelIds) // Show the known free models immediately
    }

    Process {
        id: ensureAiTempDir
        running: true
        command: ["mkdir", "-p", "/tmp/quickshell/ai", "/tmp/quickshell/ai/opencode-work"]
    }

    function guessModelLogo(model) {
        if (model.includes("llama")) return "ollama-symbolic";
        if (model.includes("gemma")) return "google-gemini-symbolic";
        if (model.includes("deepseek")) return "deepseek-symbolic";
        if (/^phi\d*:/i.test(model)) return "microsoft-symbolic";
        return "ollama-symbolic";
    }

    function guessModelName(model) {
        const replaced = model.replace(/-/g, ' ').replace(/:/g, ' ');
        let words = replaced.split(' ');
        words[words.length - 1] = words[words.length - 1].replace(/(\d+)b$/, (_, num) => `${num}B`)
        words = words.map((word) => {
            return (word.charAt(0).toUpperCase() + word.slice(1))
        });
        if (words[words.length - 1] === "Latest") words.pop();
        else words[words.length - 1] = `(${words[words.length - 1]})`; // Surround the last word with square brackets
        const result = words.join(' ');
        return result;
    }

    function addModel(modelName, data) {
        root.dynamicModels = Object.assign({}, root.dynamicModels, {
            [modelName]: aiModelComponent.createObject(this, data)
        });
    }

    Process {
        id: getOllamaModels
        running: true
        command: ["bash", "-c", `${Directories.scriptPath}/ai/show-installed-ollama-models.sh`.replace(/file:\/\//, "")]
        stdout: SplitParser {
            onRead: data => {
                try {
                    if (data.length === 0) return;
                    const dataJson = JSON.parse(data);
                    dataJson.forEach(model => {
                        const safeModelName = root.safeModelName(model);
                        root.addModel(safeModelName, {
                            "name": guessModelName(model),
                            "icon": guessModelLogo(model),
                            "description": Translation.tr("Local Ollama model | %1").arg(model),
                            "homepage": `https://ollama.com/library/${model}`,
                            "endpoint": "http://localhost:11434/v1/chat/completions",
                            "model": model,
                            "requires_key": false,
                        })
                    });
                } catch (e) {
                    console.log("Could not fetch Ollama models:", e);
                }
            }
        }
    }

    Process {
        id: getOpencodeModels
        running: true
        command: ["bash", "-c", "opencode models 2>/dev/null"]
        stdout: StdioCollector {
            onStreamFinished: {
                const ids = text.split("\n")
                    .map(line => line.trim())
                    .filter(line => line.startsWith("opencode/"))
                    .map(line => line.substring("opencode/".length))
                    .filter(id => id === "big-pickle" || id.endsWith("-free"));
                root.addOpencodeModels(ids.length > 0 ? ids : root.fallbackOpencodeModelIds);
            }
        }
        onExited: (exitCode, exitStatus) => {
            if (!root.opencodeModelsLoaded) {
                root.addOpencodeModels(root.fallbackOpencodeModelIds);
            }
        }
    }

    Process {
        id: getOpencodeSessions
        running: false
        command: ["bash", "-c", `cd '${CF.StringUtils.shellSingleQuoteEscape(root.resolveOpencodeWorkDir())}' 2>/dev/null && opencode session list --format json --max-count 20 2>/dev/null`]
        stdout: StdioCollector {
            onStreamFinished: {
                let sessions = [];
                try {
                    const parsed = JSON.parse(text.length > 0 ? text : "[]");
                    if (Array.isArray(parsed)) sessions = parsed;
                } catch (e) {
                    console.log("[AI][opencode] Could not parse sessions:", e);
                }
                root.opencodeSessions = sessions;
                if (root.opencodeSessionsRequested) {
                    root.opencodeSessionsRequested = false;
                    root.printOpencodeSessions();
                }
            }
        }
    }

    Process {
        id: exportOpencodeSession
        property string sessionId: ""
        running: false
        command: ["bash", "-c", `cd '${CF.StringUtils.shellSingleQuoteEscape(root.resolveOpencodeWorkDir())}' 2>/dev/null && opencode session export '${CF.StringUtils.shellSingleQuoteEscape(exportOpencodeSession.sessionId)}' 2>/dev/null`]
        stdout: StdioCollector {
            onStreamFinished: {
                if (text.length > 0) root.importOpencodeSession(text);
                else root.addMessage(Translation.tr("Could not load that OpenCode session."), root.interfaceRole);
            }
        }
    }

    Process {
        id: getDefaultPrompts
        running: true
        command: ["ls", "-1", Directories.defaultAiPrompts]
        stdout: StdioCollector {
            onStreamFinished: {
                if (text.length === 0) return;
                root.defaultPrompts = text.split("\n")
                    .filter(fileName => fileName.endsWith(".md") || fileName.endsWith(".txt"))
                    .map(fileName => `${Directories.defaultAiPrompts}/${fileName}`)
            }
        }
    }

    Process {
        id: getUserPrompts
        running: true
        command: ["ls", "-1", Directories.userAiPrompts]
        stdout: StdioCollector {
            onStreamFinished: {
                if (text.length === 0) return;
                root.userPrompts = text.split("\n")
                    .filter(fileName => fileName.endsWith(".md") || fileName.endsWith(".txt"))
                    .map(fileName => `${Directories.userAiPrompts}/${fileName}`)
            }
        }
    }

    Process {
        id: getSavedChats
        running: true
        command: ["ls", "-1", Directories.aiChats]
        stdout: StdioCollector {
            onStreamFinished: {
                if (text.length === 0) return;
                root.savedChats = text.split("\n")
                    .filter(fileName => fileName.endsWith(".json"))
                    .map(fileName => `${Directories.aiChats}/${fileName}`)
            }
        }
    }

    FileView {
        id: promptLoader
        watchChanges: false;
        onLoadedChanged: {
            if (!promptLoader.loaded) return;
            Config.options.ai.systemPrompt = promptLoader.text();
            root.addMessage(Translation.tr("Loaded the following system prompt\n\n---\n\n%1").arg(Config.options.ai.systemPrompt), root.interfaceRole);
        }
    }

    function printPrompt() {
        root.addMessage(Translation.tr("The current system prompt is\n\n---\n\n%1").arg(Config.options.ai.systemPrompt), root.interfaceRole);
    }

    function loadPrompt(filePath) {
        promptLoader.path = "" // Unload
        promptLoader.path = filePath; // Load
        promptLoader.reload();
    }

    function addMessage(message, role) {
        if (message.length === 0) return;
        const aiMessage = aiMessageComponent.createObject(root, {
            "role": role,
            "content": message,
            "rawContent": message,
            "thinking": false,
            "done": true,
        });
        const id = idForMessage(aiMessage);
        root.messageIDs = [...root.messageIDs, id];
        root.messageByID[id] = aiMessage;
    }

    function removeMessage(index) {
        if (index < 0 || index >= messageIDs.length) return;
        const id = root.messageIDs[index];
        root.messageIDs.splice(index, 1);
        root.messageIDs = [...root.messageIDs];
        delete root.messageByID[id];
    }

    function addApiKeyAdvice(model) {
        root.addMessage(
            Translation.tr('To set an API key, pass it with the %4 command\n\nTo view the key, pass "get" with the command<br/>\n\n### For %1:\n\n**Link**: %2\n\n%3')
                .arg(model.name).arg(model.key_get_link).arg(model.key_get_description ?? Translation.tr("<i>No further instruction provided</i>")).arg("/key"), 
            Ai.interfaceRole
        );
    }

    function getModel() {
        return models[currentModelId];
    }

    function setModel(modelId, feedback = true, setPersistentState = true) {
        if (!modelId) modelId = ""
        modelId = modelId.toLowerCase()
        if (modelId.startsWith("opencode/")) modelId = root.safeModelName(modelId)
        if (modelList.indexOf(modelId) !== -1) {
            const model = models[modelId]
            // See if policy prevents online models
            if (Config.options.policies.ai === 2 && !model.endpoint.includes("localhost")) {
                root.addMessage(
                    Translation.tr("Online models disallowed\n\nControlled by `policies.ai` config option"),
                    root.interfaceRole
                );
                return;
            }
            if (setPersistentState) Persistent.states.ai.model = modelId;
            if (feedback) root.addMessage(Translation.tr("Model set to %1").arg(model.name), root.interfaceRole);
            if (model.requires_key) {
                // If key not there show advice
                if (root.apiKeysLoaded && (!root.apiKeys[model.key_id] || root.apiKeys[model.key_id].length === 0)) {
                    root.addApiKeyAdvice(model)
                }
            }
        } else {
            if (feedback) root.addMessage(Translation.tr("Invalid model. Supported: \n```\n") + modelList.join("\n```\n```\n"), Ai.interfaceRole) + "\n```"
        }
    }

    function setTool(tool) {
        if (!root.tools[models[currentModelId]?.api_format] || !(tool in root.tools[models[currentModelId]?.api_format])) {
            root.addMessage(Translation.tr("Invalid tool. Supported tools:\n- %1").arg(root.availableTools.join("\n- ")), root.interfaceRole);
            return false;
        }
        Config.options.ai.tool = tool;
        return true;
    }
    
    function getTemperature() {
        return root.temperature;
    }

    function setTemperature(value) {
        if (value == NaN || value < 0 || value > 2) {
            root.addMessage(Translation.tr("Temperature must be between 0 and 2"), Ai.interfaceRole);
            return;
        }
        Persistent.states.ai.temperature = value;
        root.temperature = value;
        root.addMessage(Translation.tr("Temperature set to %1").arg(value), Ai.interfaceRole);
    }

    function setApiKey(key) {
        const model = models[currentModelId];
        if (!model.requires_key) {
            root.addMessage(Translation.tr("%1 does not require an API key").arg(model.name), Ai.interfaceRole);
            return;
        }
        if (!key || key.length === 0) {
            const model = models[currentModelId];
            root.addApiKeyAdvice(model)
            return;
        }
        KeyringStorage.setNestedField(["apiKeys", model.key_id], key.trim());
        root.addMessage(Translation.tr("API key set for %1").arg(model.name), Ai.interfaceRole);
    }

    function printApiKey() {
        const model = models[currentModelId];
        if (model.requires_key) {
            const key = root.apiKeys[model.key_id];
            if (key) {
                root.addMessage(Translation.tr("API key:\n\n```txt\n%1\n```").arg(key), Ai.interfaceRole);
            } else {
                root.addMessage(Translation.tr("No API key set for %1").arg(model.name), Ai.interfaceRole);
            }
        } else {
            root.addMessage(Translation.tr("%1 does not require an API key").arg(model.name), Ai.interfaceRole);
        }
    }

    function printTemperature() {
        root.addMessage(Translation.tr("Temperature: %1").arg(root.temperature), Ai.interfaceRole);
    }

    function clearMessages() {
        root.messageIDs = [];
        root.messageByID = ({});
        root.opencodeSessionId = "";
        root.tokenCount.input = -1;
        root.tokenCount.output = -1;
        root.tokenCount.total = -1;
    }

    FileView {
        id: requesterScriptFile
    }

    Process {
        id: requester
        property list<string> baseCommand: ["bash"]
        property AiMessageData message
        property ApiStrategy currentStrategy

        function markDone() {
            requester.message.done = true;
            if (root.postResponseHook) {
                root.postResponseHook();
                root.postResponseHook = null; // Reset hook after use
            }
            root.saveChat("lastSession")
            root.responseFinished()
        }

        function makeRequest() {
            const model = (models ?? {})[currentModelId];
            if (!model || !root.currentApiStrategy) {
                root.addMessage(Translation.tr("No model available. Enable online models or check the AI policy."), root.interfaceRole);
                return;
            }

            // Fetch API keys if needed
            if (model.requires_key && !KeyringStorage.loaded) KeyringStorage.fetchKeyringData();
            
            requester.currentStrategy = root.currentApiStrategy;
            requester.currentStrategy.reset(); // Reset strategy state

            /* Put API key in environment variable */
            if (model.requires_key) requester.environment[`${root.apiKeyEnvVarName}`] = root.apiKeys ? (root.apiKeys[model.key_id] ?? "") : ""

            /* Build endpoint, request data */
            const endpoint = root.currentApiStrategy.buildEndpoint(model);
            const messageArray = root.messageIDs.map(id => root.messageByID[id]);
            const filteredMessageArray = messageArray.filter(message => message.role !== Ai.interfaceRole);
            if (model.api_format === "opencode") {
                requester.currentStrategy.agent = root.resolveOpencodeAgent();
                requester.currentStrategy.sessionId = root.opencodeSessionId;
            }
            const data = root.currentApiStrategy.buildRequestData(model, filteredMessageArray, root.systemPrompt, root.temperature, root.tools[model.api_format][root.currentTool], root.pendingFilePath);
            // console.log("[Ai] Request data: ", JSON.stringify(data, null, 2));

            let requestHeaders = {
                "Content-Type": "application/json",
            }
            
            /* Create local message object */
            requester.message = root.aiMessageComponent.createObject(root, {
                "role": "assistant",
                "model": currentModelId,
                "content": "",
                "rawContent": "",
                "thinking": true,
                "done": false,
            });
            const id = idForMessage(requester.message);
            root.messageIDs = [...root.messageIDs, id];
            root.messageByID[id] = requester.message;

            /* Build header string for curl */ 
            let headerString = Object.entries(requestHeaders)
                .filter(([k, v]) => v && v.length > 0)
                .map(([k, v]) => `-H '${k}: ${v}'`)
                .join(' ');

            // console.log("Request headers: ", JSON.stringify(requestHeaders));
            // console.log("Header string: ", headerString);

            /* Get authorization header from strategy */
            const authHeader = requester.currentStrategy.buildAuthorizationHeader(root.apiKeyEnvVarName);
            
            /* Script shebang */
            const scriptShebang = "#!/usr/bin/env bash\n";

            /* Capture the attachment before the generic setup clears it. */
            const attachedPath = root.pendingFilePath;

            /* Create extra setup when there's an attached file */
            let scriptFileSetupContent = ""
            if (root.pendingFilePath && root.pendingFilePath.length > 0) {
                requester.message.localFilePath = root.pendingFilePath;
                scriptFileSetupContent = requester.currentStrategy.buildScriptFileSetup(root.pendingFilePath);
                root.pendingFilePath = ""
            }

            /* OpenCode free models are executed through the CLI instead of curl */
            if (model.api_format === "opencode") {
                const workDir = root.resolveOpencodeWorkDir();
                const shellScriptPath = CF.FileUtils.trimFileProtocol(root.requestScriptFilePath);
                const promptDelimiter = `OPENCODE_PROMPT_${Date.now()}_${Math.random().toString(36).slice(2)}`;
                const escapedWorkDir = CF.StringUtils.shellSingleQuoteEscape(workDir);
                const escapedAttachedPath = CF.StringUtils.shellSingleQuoteEscape(attachedPath ?? "");
                const agent = root.resolveOpencodeAgent();
                const escapedAgent = CF.StringUtils.shellSingleQuoteEscape(agent);
                const sessionId = (root.opencodeSessionId ?? "").trim();
                const escapedSessionId = CF.StringUtils.shellSingleQuoteEscape(sessionId);

                const scriptContent = `#!/usr/bin/env bash
set -uo pipefail
exec </dev/null
if ! command -v opencode >/dev/null 2>&1; then
    echo '{"type":"error","error":{"message":"OpenCode CLI is not installed."}}'
    exit 1
fi
attached_path='${escapedAttachedPath}'
work_dir='${escapedWorkDir}'
if [ ! -d "$work_dir" ]; then
    echo '{"type":"error","error":{"message":"OpenCode working folder not found. Set a valid folder with /folder PATH or in Settings → Services → AI."}}'
    exit 1
fi
cd "$work_dir" || exit 1
session_args=()
if [ -n '${escapedSessionId}' ]; then
    session_args=(--session '${escapedSessionId}')
fi
file_args=()
if [ -n "$attached_path" ] && [ ! -d "$attached_path" ]; then
    if [ ! -f "$attached_path" ]; then
        echo '{"type":"error","error":{"message":"Attached file not found."}}'
        exit 1
    fi
    file_args=(--file "$attached_path")
fi
prompt=$(cat <<'${promptDelimiter}'
${data.prompt}
${promptDelimiter}
)
opencode run --agent '${escapedAgent}' --model '${CF.StringUtils.shellSingleQuoteEscape(model.model)}' --format json "\${session_args[@]}" "\${file_args[@]}" "$prompt"
`;

                requesterScriptFile.path = Qt.resolvedUrl(shellScriptPath);
                requesterScriptFile.setText(scriptContent);
                requester.command = baseCommand.concat([shellScriptPath]);
                requester.running = true;
                return;
            }

            /* Create command string */
            let scriptRequestContent = ""
            scriptRequestContent += `curl --no-buffer "${endpoint}"`
                + ` ${headerString}`
                + (authHeader ? ` ${authHeader}` : "")
                + ` --data '${CF.StringUtils.shellSingleQuoteEscape(JSON.stringify(data))}'`
                + "\n"
            
            /* Send the request */
            const scriptContent = requester.currentStrategy.finalizeScriptContent(scriptShebang + scriptFileSetupContent + scriptRequestContent)
            const shellScriptPath = CF.FileUtils.trimFileProtocol(root.requestScriptFilePath)
            requesterScriptFile.path = Qt.resolvedUrl(shellScriptPath)
            requesterScriptFile.setText(scriptContent)
            requester.command = baseCommand.concat([shellScriptPath]);
            requester.running = true
        }

        stdout: SplitParser {
            onRead: data => {
                if (data.length === 0) return;
                if (requester.message.thinking) requester.message.thinking = false;
                // console.log("[Ai] Raw response line: ", data);

                // Handle response line
                try {
                    const result = requester.currentStrategy.parseResponseLine(data, requester.message);
                    // console.log("[Ai] Parsed response result: ", JSON.stringify(result, null, 2));

                    if (result.sessionId) {
                        root.opencodeSessionId = result.sessionId;
                    }
                    if (result.functionCall) {
                        requester.message.functionCall = result.functionCall;
                        root.handleFunctionCall(result.functionCall.name, result.functionCall.args, requester.message);
                    }
                    if (result.tokenUsage) {
                        root.tokenCount.input = result.tokenUsage.input;
                        root.tokenCount.output = result.tokenUsage.output;
                        root.tokenCount.total = result.tokenUsage.total;
                    }
                    if (result.finished) {
                        requester.markDone();
                    }
                    
                } catch (e) {
                    console.log("[AI] Could not parse response: ", e);
                    requester.message.rawContent += data;
                    requester.message.content += data;
                }
            }
        }

        stderr: SplitParser {
            onRead: data => {
                if (data.length > 0) console.log("[AI] stderr:", data);
            }
        }

        onExited: (exitCode, exitStatus) => {
            const result = requester.currentStrategy.onRequestFinished(requester.message);
            
            if (result.finished) {
                requester.markDone();
            } else if (!requester.message.done) {
                requester.markDone();
            }

            // Handle error responses
            if (requester.message.content.includes("API key not valid")) {
                root.addApiKeyAdvice(models[requester.message.model]);
            }
        }
    }

    function sendUserMessage(message) {
        if (message.length === 0) return;
        root.addMessage(message, "user");
        requester.makeRequest();
    }

    function attachFile(filePath: string) {
        root.pendingFilePath = root.resolveUserPath(filePath);
    }

    function regenerate(messageIndex) {
        if (messageIndex < 0 || messageIndex >= messageIDs.length) return;
        const id = root.messageIDs[messageIndex];
        const message = root.messageByID[id];
        if (message.role !== "assistant") return;
        // Remove all messages after this one
        for (let i = root.messageIDs.length - 1; i >= messageIndex; i--) {
            root.removeMessage(i);
        }
        requester.makeRequest();
    }

    function createFunctionOutputMessage(name, output, includeOutputInChat = true) {
        return aiMessageComponent.createObject(root, {
            "role": "user",
            "content": `[[ Output of ${name} ]]${includeOutputInChat ? ("\n\n<think>\n" + output + "\n</think>") : ""}`,
            "rawContent": `[[ Output of ${name} ]]${includeOutputInChat ? ("\n\n<think>\n" + output + "\n</think>") : ""}`,
            "functionName": name,
            "functionResponse": output,
            "thinking": false,
            "done": true,
            // "visibleToUser": false,
        });
    }

    function addFunctionOutputMessage(name, output) {
        const aiMessage = createFunctionOutputMessage(name, output);
        const id = idForMessage(aiMessage);
        root.messageIDs = [...root.messageIDs, id];
        root.messageByID[id] = aiMessage;
    }

    function rejectCommand(message: AiMessageData) {
        if (!message.functionPending) return;
        message.functionPending = false; // User decided, no more "thinking"
        addFunctionOutputMessage(message.functionName, Translation.tr("Command rejected by user"))
    }

    function approveCommand(message: AiMessageData) {
        if (!message.functionPending) return;
        message.functionPending = false; // User decided, no more "thinking"

        const responseMessage = createFunctionOutputMessage(message.functionName, "", false);
        const id = idForMessage(responseMessage);
        root.messageIDs = [...root.messageIDs, id];
        root.messageByID[id] = responseMessage;

        commandExecutionProc.message = responseMessage;
        commandExecutionProc.baseMessageContent = responseMessage.content;
        commandExecutionProc.shellCommand = message.functionCall.args.command;
        commandExecutionProc.running = true; // Start the command execution
    }

    Process {
        id: commandExecutionProc
        property string shellCommand: ""
        property AiMessageData message
        property string baseMessageContent: ""
        command: ["bash", "-c", shellCommand]
        stdout: SplitParser {
            onRead: (output) => {
                commandExecutionProc.message.functionResponse += output + "\n\n";
                const updatedContent = commandExecutionProc.baseMessageContent + `\n\n<think>\n<tt>${commandExecutionProc.message.functionResponse}</tt>\n</think>`;
                commandExecutionProc.message.rawContent = updatedContent;
                commandExecutionProc.message.content = updatedContent;
            }
        }
        onExited: (exitCode, exitStatus) => {
            commandExecutionProc.message.functionResponse += `[[ Command exited with code ${exitCode} (${exitStatus}) ]]\n`;
            requester.makeRequest(); // Continue
        }
    }

    function handleFunctionCall(name, args: var, message: AiMessageData) {
        if (name === "switch_to_search_mode") {
            const modelId = root.currentModelId;
            root.currentTool = "search"
            root.postResponseHook = () => { root.currentTool = "functions" }
            addFunctionOutputMessage(name, Translation.tr("Switched to search mode. Continue with the user's request."))
            requester.makeRequest();
        } else if (name === "get_shell_config") {
            const configJson = CF.ObjectUtils.toPlainObject(Config.options)
            addFunctionOutputMessage(name, JSON.stringify(configJson));
            requester.makeRequest();
        } else if (name === "set_shell_config") {
            if (!args.key || !args.value) {
                addFunctionOutputMessage(name, Translation.tr("Invalid arguments. Must provide `key` and `value`."));
                return;
            }
            const key = args.key;
            const value = args.value;
            Config.setNestedValue(key, value);
        } else if (name === "run_shell_command") {
            if (!args.command || args.command.length === 0) {
                addFunctionOutputMessage(name, Translation.tr("Invalid arguments. Must provide `command`."));
                return;
            }
            const contentToAppend = `\n\n**Command execution request**\n\n\`\`\`command\n${args.command}\n\`\`\``;
            message.rawContent += contentToAppend;
            message.content += contentToAppend;
            message.functionPending = true; // Use thinking to indicate the command is waiting for approval
        }
        else root.addMessage(Translation.tr("Unknown function call: %1").arg(name), "assistant");
    }

    function chatToJson() {
        return root.messageIDs.map(id => {
            const message = root.messageByID[id]
            return ({
                "role": message.role,
                "rawContent": message.rawContent,
                "fileMimeType": message.fileMimeType,
                "fileUri": message.fileUri,
                "localFilePath": message.localFilePath,
                "model": message.model,
                "thinking": false,
                "done": true,
                "annotations": message.annotations,
                "annotationSources": message.annotationSources,
                "functionName": message.functionName,
                "functionCall": message.functionCall,
                "functionResponse": message.functionResponse,
                "visibleToUser": message.visibleToUser,
            })
        })
    }

    FileView {
        id: chatSaveFile
        property string chatName: ""
        blockLoading: true // Prevent race conditions
    }

    function chatPathForName(chatName) {
        return `${Directories.aiChats}/${chatName.trim()}.json`;
    }

    function prepareChatFile(chatName) {
        chatSaveFile.chatName = chatName.trim()
        // Reset the FileView state first. Otherwise loading a second chat can
        // return the previously loaded file's text before the async read ends.
        chatSaveFile.path = ""
        chatSaveFile.path = root.chatPathForName(chatName)
    }

    /**
     * Saves chat messages and the current OpenCode session ID.
     * @param chatName name of the chat
     */
    function saveChat(chatName) {
        root.prepareChatFile(chatName)
        const saveContent = JSON.stringify({
            version: 2,
            savedAt: Date.now(),
            opencodeSessionId: root.opencodeSessionId,
            messages: root.chatToJson(),
        })
        chatSaveFile.setText(saveContent)
        getSavedChats.running = true;
    }

    /**
     * Loads a saved chat. Supports the old array-only format and the new
     * object format that restores the OpenCode session ID.
     * @param chatName name of the chat
     */
    function loadChat(chatName) {
        try {
            root.prepareChatFile(chatName)
            chatSaveFile.reload()
            const saveContent = chatSaveFile.text()
            // console.log(saveContent)
            const saveData = JSON.parse(saveContent)
            const messages = Array.isArray(saveData) ? saveData : (saveData.messages ?? [])
            const sessionId = Array.isArray(saveData) ? "" : (saveData.opencodeSessionId ?? "")
            root.clearMessages()
            root.opencodeSessionId = sessionId
            root.messageIDs = messages.map((_, i) => {
                return i
            })
            // console.log(JSON.stringify(messageIDs))
            for (let i = 0; i < messages.length; i++) {
                const message = messages[i];
                root.messageByID[i] = root.aiMessageComponent.createObject(root, {
                    "role": message.role,
                    "rawContent": message.rawContent,
                    "content": message.rawContent,
                    "fileMimeType": message.fileMimeType,
                    "fileUri": message.fileUri,
                    "localFilePath": message.localFilePath,
                    "model": message.model,
                    "thinking": message.thinking,
                    "done": message.done,
                    "annotations": message.annotations,
                    "annotationSources": message.annotationSources,
                    "functionName": message.functionName,
                    "functionCall": message.functionCall,
                    "functionResponse": message.functionResponse,
                    "visibleToUser": message.visibleToUser,
                });
            }
        } catch (e) {
            console.log("[AI] Could not load chat: ", e);
        } finally {
            getSavedChats.running = true;
        }
    }
}

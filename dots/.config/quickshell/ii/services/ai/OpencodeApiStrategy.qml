import QtQuick

/**
 * OpenAI-compatible strategy that routes through the OpenCode CLI.
 *
 * OpenCode's free tier is deliberately only usable from inside OpenCode, so
 * instead of calling the inference endpoint directly we ask the installed
 * `opencode` CLI to run the chat. The CLI already knows how to authenticate
 * and select the requested free model.
 *
 * The widget keeps an OpenCode session ID so subsequent messages continue the
 * same conversation. The selected agent decides whether files can be edited.
 */
ApiStrategy {
    // Set by Ai.qml before each request.
    property string sessionId: ""
    property string agent: "build"

    function buildEndpoint(model: AiModel): string {
        return model.endpoint;
    }

    function buildRequestData(model: AiModel, messages, systemPrompt: string, temperature: real, tools: list<var>, filePath: string) {
        const isNewSession = (sessionId ?? "").trim().length === 0;
        const lastUserMessage = [...messages].reverse().find(message => message.role === "user");
        const userMessageStart = "<<<USER_MESSAGE>>>";
        const userMessageEnd = "<<<END_USER_MESSAGE>>>";
        const history = messages.map(message => {
            const content = (message.rawContent ?? message.content ?? "").trim();
            if (content.length === 0) return "";
            const role = (message.role === "user") ? "User" : "Assistant";
            const markedContent = message === lastUserMessage
                ? `${userMessageStart}\n${content}\n${userMessageEnd}`
                : content;
            return `### ${role}\n${markedContent}`;
        }).filter(part => part.length > 0).join("\n\n");

        const canEdit = agent !== "plan";
        const promptParts = [
            "You are the AI assistant inside the user's desktop sidebar.",
            "Treat this as a normal conversation.",
            canEdit
                ? "You may inspect and edit files in the current working folder when the user asks."
                : "You have read-only access to the current working folder. Inspect files there when the user asks, but do not modify them.",
            "Do not use tools for anything else unless the user explicitly asks.",
            filePath && filePath.length > 0 ? `The user attached this file or folder: ${filePath}\nUse the absolute path when accessing it.` : "",
            "",
        ];

        if (isNewSession) {
            promptParts.push(
                "## Custom instructions",
                systemPrompt || "(none)",
                "",
                "## Conversation so far",
                history,
                ""
            );
        } else {
            promptParts.push(
                "## New message",
                `${userMessageStart}\n${(lastUserMessage?.rawContent ?? lastUserMessage?.content ?? "").trim()}\n${userMessageEnd}`,
                ""
            );
        }

        promptParts.push(
            "## Your next reply",
            "Reply only with the assistant's next message. Do not add a role label."
        );
        const prompt = promptParts.join("\n");

        return {
            "prompt": prompt,
            "model": model.model,
            "temperature": temperature,
        };
    }

    function buildAuthorizationHeader(apiKeyEnvVarName: string): string {
        return "";
    }

    function parseResponseLine(line, message) {
        const cleanLine = line.trim();
        if (cleanLine.length === 0) return {};

        // `opencode run --format json` prints one JSON event per line. Ignore
        // any human-readable notices that may appear around those events.
        if (!cleanLine.startsWith("{")) {
            if (/error|failed|not found/i.test(cleanLine)) {
                const errorText = `**OpenCode error**: ${cleanLine}`;
                message.rawContent += errorText;
                message.content += errorText;
                return { finished: true };
            }
            return {};
        }

        let eventSessionId = "";
        try {
            const event = JSON.parse(cleanLine);
            if (event.sessionID) eventSessionId = event.sessionID;
            if (event.type === "text" && event.part?.text) {
                message.rawContent += event.part.text;
                message.content += event.part.text;
            } else if (event.type === "error") {
                const detail = event.error?.message ?? event.error ?? "Unknown error";
                const errorText = `**OpenCode error**: ${detail}`;
                message.rawContent += errorText;
                message.content += errorText;
                return { finished: true, sessionId: eventSessionId };
            }
        } catch (e) {
            console.log("[AI][opencode] Could not parse response line:", e);
        }

        return eventSessionId.length > 0 ? { sessionId: eventSessionId } : {};
    }

    function onRequestFinished(message) {
        if ((message.content ?? "").length === 0) {
            const noResponse = "OpenCode did not return a response.";
            message.rawContent += noResponse;
            message.content += noResponse;
        }
        return { finished: true };
    }

    function reset() {
    }
}

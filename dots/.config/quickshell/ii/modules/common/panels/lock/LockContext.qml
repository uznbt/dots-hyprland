import qs
import qs.modules.common
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Services.Pam

Scope {
    id: root

    enum ActionEnum { Unlock, Poweroff, Reboot }

    signal shouldReFocus()
    signal unlocked(targetAction: var)
    signal failed()

    // These properties are in the context and not individual lock surfaces
    // so all surfaces can share the same state.
    property string currentText: ""
    property bool unlockInProgress: false
    property bool showFailure: false
    property bool fingerprintsConfigured: false
    property bool fingerprintPaused: false
    property bool fingerPamStarting: false
    property int fingerprintAttempts: 0
    property var targetAction: LockContext.ActionEnum.Unlock
    property bool alsoInhibitIdle: false

    function resetTargetAction() {
        root.targetAction = LockContext.ActionEnum.Unlock;
    }

    function clearText() {
        root.currentText = "";
    }

    function resetClearTimer() {
        passwordClearTimer.restart();
    }

    function reset() {
        root.resetTargetAction();
        root.clearText();
        root.unlockInProgress = false;
        root.fingerprintPaused = false;
        root.fingerPamStarting = false;
        root.fingerprintAttempts = 0;
        stopFingerPam();
    }

    Timer {
        id: passwordClearTimer
        interval: 10000
        onTriggered: {
            root.reset();
        }
    }

    onCurrentTextChanged: {
        if (currentText.length > 0) {
            showFailure = false;
            GlobalStates.screenUnlockFailed = false;
        }
        GlobalStates.screenLockContainsCharacters = currentText.length > 0;
        passwordClearTimer.restart();
    }

    function tryUnlock(alsoInhibitIdle = false) {
        root.alsoInhibitIdle = alsoInhibitIdle;
        root.unlockInProgress = true;
        pam.start();
    }

    function tryFingerUnlock() {
        if (root.fingerprintPaused || !root.fingerprintsConfigured
                || fingerPam.active || root.fingerPamStarting)
            return;
        if (root.fingerprintAttempts >= 3)
            return;

        root.fingerprintAttempts += 1;
        root.fingerPamStarting = true;
        if (!fingerPam.start())
            root.fingerPamStarting = false;
    }

    function retryFingerUnlock() {
        root.fingerprintPaused = false;
        root.fingerPamStarting = false;
        root.fingerprintAttempts = 0;
        root.tryFingerUnlock();
    }

    function pauseFingerprint() {
        root.fingerprintPaused = true;
        root.stopFingerPam();
    }

    function resumeFingerprint() {
        root.fingerprintPaused = false;
        root.fingerprintAttempts = 0;
        if (GlobalStates.screenLocked && root.fingerprintsConfigured)
            root.tryFingerUnlock();
    }

    function refreshFingerprintStatus() {
        if (!fingerprintCheckProc.running) {
            fingerprintCheckProc.running = true;
        }
    }

    function stopFingerPam() {
        fingerRetryTimer.stop();
        root.fingerPamStarting = false;
        if (fingerPam.active) {
            fingerPam.abort();
        }
    }

    Timer {
        id: fingerRetryTimer
        interval: 900
        repeat: false
        onTriggered: root.tryFingerUnlock()
    }

    Process {
        id: fingerprintCheckProc
        running: true
        command: ["bash", "-c", "fprintd-list $(whoami)"]
        stdout: StdioCollector {
            id: fingerprintOutputCollector
            onStreamFinished: {
                root.fingerprintsConfigured = fingerprintOutputCollector.text.includes("Fingerprints for user");
                if (root.fingerprintsConfigured && GlobalStates.screenLocked) {
                    root.fingerprintAttempts = 0;
                    root.tryFingerUnlock();
                }
            }
        }
        onExited: (exitCode, exitStatus) => {
            if (exitCode !== 0) {
                // console.warn("[LockContext] fprintd-list command exited with error:", exitCode, exitStatus);
                root.fingerprintsConfigured = false;
            }
        }
    }
    
    PamContext {
        id: pam

        // pam_unix will ask for a response for the password prompt
        onPamMessage: {
            if (this.responseRequired) {
                this.respond(root.currentText);
            }
        }

        // pam_unix won't send any important messages so all we need is the completion status.
        onCompleted: result => {
            if (result == PamResult.Success) {
                root.unlocked(root.targetAction);
                stopFingerPam();
            } else {
                root.clearText();
                root.unlockInProgress = false;
                GlobalStates.screenUnlockFailed = true;
                root.showFailure = true;
            }
        }
    }

    PamContext {
        id: fingerPam

        configDirectory: "pam"
        config: "fprintd.conf"

        onCompleted: result => {
            root.fingerPamStarting = false;
            if (result == PamResult.Success) {
                root.unlocked(root.targetAction);
                stopFingerPam();
            } else if (result == PamResult.Failed || result == PamResult.MaxTries) {
                // A finger was read but not recognised. Keep trying briefly,
                // then let the user retry from the fingerprint button.
                if (root.fingerprintAttempts < 3)
                    fingerRetryTimer.restart();
            } else if (result == PamResult.Error) {
                // pam_fprintd timed out waiting for a finger. Restart it while
                // the lock screen is on; pauseFingerprint() stops this loop
                // when the display goes to sleep.
                root.fingerprintAttempts = 0;
                if (!root.fingerprintPaused)
                    fingerRetryTimer.restart();
            }
        }

        onError: error => {
            root.fingerPamStarting = false;
        }
    }
}

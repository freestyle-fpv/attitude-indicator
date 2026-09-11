# Latest confirmed work — shared calibration button

Right-upper PWR (screen facing user,USBleft) singleclick now requests shared calibration throughTCA9554P4. BOOT runtime short/longactionsremoved;PWR configurablelongshutdown disabled. Debounce in samplingtask100Hz;oneclick/release,ignorelong/invalidI2C/startupheld. New calibration session token prevents stale completion;progressmodal restoresselecteddial;attitude/HUDdemodisabledoncal. ActualIAS/ALT/HDG remain simulated.

User confirmed calibration normal and originaldial restored.90sraw docs/shared-calibration-board-measurements.json captures exactlyonePWRclick,startid1,completeid1nearzeropitchroll;allfourmodes subsequentlysamecalid1,HUDstill30.3FPSwithconfirmedantitear timingunchanged. Buildflashverified;testsbutton/calibration/controls/touch/attitudepass. No pending tools/questions. Docs/shared-calibration-validation.md hasfinalSHA. No commit/push.

Earlier HUD history follows:

# Confirmed outcome

用户最新回复“好了”：第二版实物撕裂已消失，保留当前固件。没有待回复的问题。运动演示约30.3 FPS；后续性能优化必须保留已确认的传输时序。

以下为调试过程记录，里面的等待反馈状态已被本确认替代。

# Latest checkpoint — HUD tearing, second trial awaiting user

Original complaint: tilted central pitch ladder tears when pitch changes. Trial1 changed dirty packing to one horizontal envelope per16row band to remove backward Y writes. Old test failed at roll35,pitch0.37 (row128 then112). New targeted and350frame reconstruction/order tests pass. User explicitly reported trial1 had NO improvement.

Then instrumented pixel DMA start/end in ISR callbacks (FastPanel::RegionTiming[40], user points to timing for all3window packets, only0x32pixel callbacks store times), PanelSync::lastEdge, DisplayStats scanMinUs/scanMaxUs, serial scan_us. Against uniform466row/TEperiod model, original central margins crossed -2966..+2578us. Raw docs/hud-scan-trace-board-measurements.json.

Trial2: PanelSync::wait(phaseUs=4000) waits freshTE then vTaskDelay plus microsecond tail. presentDial explicitly passes period/4 onlyHUD (period12..22ms sanity,else4000),0allotherdials. Waittimeoutreturnsfalse withnoextra4ms. New tools/panel_sync_test.sh failsbefore change,passesafter includinglegacy0delay,wrap,timeout. FastPaneltransporttests passwithdiagnostics.

Current SOURCE/BUILD/DEVICE all match SHA 7f2ab7c1fcf61c79311beccb973a96ebf2b35083d6a578a4b86f920220b1d7d1. Program649526,RAM35476. Flash hashverified build/hud-phase-flash.log. Second35s measurefinished docs/hud-scan-phase-board-measurements.json:22validdemo windows30.3–30.4FPS,median30.3. Central modelmargins allnegative -5390..-1385us. No estimatedscan crossing. ThisdoesNOTprove physicalvisualfix. Frame costlimitsFPS~30 vs35/36 prior;40targetunmet.

Device leftrealIMU HUD. Asyncquestionpending: secondfirmware,tilt30–45andvarypitch;optionsgone/improved/noimprovement. DoNOTclaim tearingfixed before feedback. Firstnoimprovement meansremainingcausecouldbe inaccurate scanmodel or visualartifact; ifsecondfails requestshortvideo/inspectscreen evidence before anotherblindchange. Nonewuserpermissionneededforflash. Noactivecommands. Docs/hud-tear-validation.md hasalltestsandrawresults;READMElinkupdated. No commits/push. Tests/panel_sync_stubs new host mocks, tests/panel_sync_test.cpp,tools/panel_sync_test.sh createdthisturn. Diagnosticrecords anddocsuntracked alongwithmanyprefiles;preserveall.








PROCEDURE Autosave (v: Views.View; IN location, fname: Files.Name);

Autosave before close is implemented using:
	BeforeCloseMsg = RECORD (Sequencers.Message)
	CloseHookDesc.Close sends a msg: BeforeCloseMsg via Sequencer.Notify
	SeqNotifier = RECORD (Sequencers.Notifier) processes BeforeCloseMsg
	PROCEDURE Autosave installs an n: SeqNotifier into a view's sequencer

	So, when (any) window is about to be closed, the notification is sent to it's sequencer, and if a notifier is installed, it processes the notification and autosaves the window's view.
This infrastructure actually allows arbitrary views to be autosaved before close.
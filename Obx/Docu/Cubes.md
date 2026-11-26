Overview by Example: ObxCubes

This example implements a simple rotating cube view. The cube rotates around two axes. It performs 10 state transformations per second and after 25.6 seconds it is back in its starting position. The example below shows a large cube rotating in front of a smaller one. Both cubes are embedded in a form view, which is embedded in the text view that you are currently reading:
			
What this example shows is the use of actions. An action (`Services.Action`) is an object whose `Do` procedure is executed in a delayed fashion, when the system is idle. An action which re-installs itself whenever it is invoked as in this example operates as a non-preemptive background task.

This example also demonstrates a simple property editor through which the colors of the sides of a cube can be changed. To open the property editor click into the cube while holding down the ctrl key (or execute the command below to open the dialog). The property editor always shows the colors of the selected cube. The synchronization between the color fields in the dialog and the selected cube is also performed through the installed action. The colors of the sides are actually changed by a notifier which is attached to each color control in the property dialog. The color white is interpreted as invisible.


 "ObxCubes.Deposit; StdCmds.Open"
 "StdCmds.OpenToolDialog('Obx/Rsrc/Cubes', 'Cube Colors')"

ObxCubes  sources

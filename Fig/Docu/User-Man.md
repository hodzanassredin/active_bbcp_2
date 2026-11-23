Fig Graphical Editor

Simple, extensible graphical editor implemented as a BlackBox view.
It works by inserting  free points into the drawing, and then connecting figures or constrained points to these. Points connected to figures or constrained points are called anchorpoints. Both free points and constrained points can become anchorpoints. So, we have the following basic entities:

FreePoint: 	The simplest building block. This point has total freedom, and can be moved around.
ConstrainedPoint: 	Constrained points must be connected to anchorpoints (either free or constrained),
	and may have some freedom. The position of a constrained point is computed based on 
	its anchorpoints.
Figure: 	Some sort of geometrical shape, such as a line or circle. Figures must be connected 
	to anchorpoints (free or contrained).

The editor can be extended with new constrained points and figures, and a few basic figures and constrained points are included.

Some basic functionalities are supported:

- Deleting points and figures.
- Joining and spliting of points.
- Adding and removing anchorpoints.
- Changing of properties via the Attributes menu.
- Changing the order of displaying figures.
- A grid.


		
General info and basic operations

Free points are displayed as crosses. Constrained points are displayed as boxes, with a small dot in the middle. Figures are obviously displayed differently, depending on their type. Selected points are drawn with a thicker line. Selected figures have a thin outline based on their bounding box drawn around them.

To select a point or a figure, just click on it. By holding down the mouse button while draging, you can select all points or figures within a rectangular area. By holding down the Extend key you can keep the old selection. Clicking on an empty spot deselects everything.

In order to delete whatever is selected at the moment, press Backspace or Delete. Points that have depending points and depending figures that aren't selected, won't be deleted.

Figures and constrained points are activated by holding down the Alt (Windows)/Command (Mac) key and clicking on the figure or point, or by doubleclicking on the figure or point. If several figures or points occupy the same place, then this operation work on the top figure or point.

Move selected points by positioning the cursor arrow on top of one of the selected points and drag it. While the mouse button is down a shadow of the free points will be seen so you can position the points. When you're satisfied with their position, just release the button and the point will be moved. All selected points will be moved at the same time, and figures connected to the moved points will be redrawn accordingly. By holding down the Modifier key while moving points, a copy of the selection will be created at the new position. Figures' anchorpoints are also copied in addition to the selected points and figures. Constrained points are copied as constrained points only if all of their anchorpoints are selected, otherwise they're transformed into free points.

The selected points can also be moved using the cursor keys, and the points are then moved in a very fine grained maner. If the Modifier key is hold down when using the cursor keys, the points are moved on the grid. Note that it's not possible to copy points while moving with the cursor keys.

Points and figures can be cut, copied and pasted (using the Edit menu). Cut will delete everything possible from the selection. Figures' anchorpoints are also copied in addition to the selected points and figures. Constrained points are copied as constrained points only if all of their anchorpoints are selected, otherwise they're transformed into free points. To paste a previously cut/copied selection, first select a point to position the upper left corner of the selection, and the execute the paste command.

	
Inserting free points
	
To create a new free point just hold down the Modify key and click on an empty spot in the editor window, or doubleclick on an empty spot. If you also hold down the Extend key, the old selection will be kept.
Doubleclicking doesn't always work, and in that case you must insert a free point the "hard" way.


Inserting figures

In order to insert a new figure, you just select at least the minimum number of points needed and choose the wanted figure from the Brahe Extensions menu. The last selected points will be used. The unused points remain selected.


Inserting constrained points

Constrained points reposition themselves in order to always be in their correct position in relation to their anchorpoints. In order to insert a new constrained point, you just select at least the minimum number of points needed and execute the command from the Brahe Extensions menu. The last selected points will be used. The unused points remain selected.

Joining and splitting points
	
Splitting
Anchorpoints that are used for several figures and/or constrained points can be splitted. Select "Split Points" from the menu when you have one point selected. This operation creates new free points so that each and every one of the selected point's dependent points and figures have a copy of their own afterwards. The splitted point is deleted after the splitting, or just deselected if it's a constrained point.
	
Joining
Anchorpoints can be joined into one single point. Select "Join Points" from the Brahe menu when you have one or several points selected. This automatically adjusts all dependent points and figures to their new anchorpoint. The last selected point becomes the join target to which all figures and constrained points become connected.

Constrained point are only allowed to join if they are the join target, and then only if they don't depend on any of the other selected points.
	
	
Adding and removing anchorpoints

Figures and constrained points defined as BraheModels.expandable can have their anchorpoints removed or have new anchorpoints added.

Adding

Figure: Select the two anchorpoints between which you want to insert a new anchorpoint, then select the figure. The anchorpoints have to be connected directly to each other, and it must be possible to determine a single figure from those two anchorpoints, for this command to work. Then just choose "Add Anchor" from the Brahe menu to add a new anchorpoint to the figure. Figure not defined as BraheModels.cyclic can have a new anchorpoint added to either end by selecting just the single endpoint + the figure, and choosing "Add Anchor" from the Brahe menu.

Constrained Point:Select the constrained point, and the choose "Add Anchor" from the Brahe menu.

Removing
Figure: Select the anchorpoint that you want to remove, then select then figure. Then just choose "Remove Anchor" from the Brahe menu. Constrained points acting as anchorpoints can't be removed in this way.

Constrained Point: Select the anchorpoint, and then select the constrained point. Then choose "Remove Anchor" from the Braeh menu.
	
Changing the order of figures

In order to move figures to a position on top of all other figures, select the figures, and then select "Bring To Front" from the menu. If you instead select "Send To Back" the figures' new position will be behind all other figures. The order of the selected figures are kept.
	
	
Hiding and showing points

By choosing "Hide Points/Show Points" from the Brahe menu you can toggle the points' visibility. Selected points are always shown. No points, except selected ones, are printed.  Points are shown as default.


Hiding and showing the grid

By choosing "Hide Grid/Show Grid" from the Brahe menu you can toggle the grid's visibility. An invisible grid is still enabled. The grid is shown as default.


Enabling and disabling the grid

By choosing "Enable Grid/Disable Grid" from the Brahe menu you can toggle the grid on and off. If the grid is enabled, all point insertions and moves are forced along the grid lines. The grid is enabled as default.


Force to Grid

By choosing "Force To Grid" from the Brahe menu, you can force all selected points to the nearest grid line. The grid has to be enabled for this command to work.


Set Grid Size...

By choosing "Set Grid Size..." you can change the grid through a dialog. Grid Drawing Factor determines how often a grid line should be drawn. A Grid Drawing factor of 2 will shown every second grid line, while a Grid Drawing Factor of 10 will shown every tenth grid line. The unit size can also be changed (mm, 1/16th inch or point). The default values are:

Unit size: Ports.mm
Grid: 3 * Unit size
Grid Drawing Factor: 3


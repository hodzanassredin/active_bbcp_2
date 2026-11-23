XhtmlExporter

This module implements an HTML exporter that can be used with current Web browsers. Its HTML output already conforms to the stricter XHTML syntax.

The (X)HTML elements/tags and attributes currently used are:
	html (xmlns), 
	head, 
	meta (http-equiv, content, charset), 
	style (type), 
	body, 
	p (text-align, margin-left, width, text-indent, margin-top, margin-bottom),
	br, 
	a (href, id),
	b,
	i,
	span (font-family, font-size, color, text-decoration).

The exporter converts text. Embedded views are not exported. Link and target views (module StdLinks) are exceptions, they are turned into HTML links and anchors. Links within documents and Dialog.OpenExternal are supported. The page title is taken from the window title if one exists, otherwise it is titled 'New Page'.

The exporter is preconfigured in StdConfig.Setup:

	Converters.Register("", "XhtmlExporter.ExportText", "TextViews.View", "html", {});

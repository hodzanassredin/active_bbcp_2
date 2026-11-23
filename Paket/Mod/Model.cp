MODULE PaketModel;
(**
	project	= "BlackBox 2.0, new module"
	organization	= "BlackBox Cross-Platform Team"
	contributors	= "Anton Shelin"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "The 2-Clause BSD License"
	changes	= ""
	issues	= ""

**)

	IMPORT TextMappers;

	TYPE
		Package* = POINTER TO RECORD
			data*: ARRAY 6 OF TextMappers.String;
			depends*: PackageList;
			isInstalled*: BOOLEAN;
		END;
		PackageList* = POINTER TO RECORD
			package*: Package;
			next*: PackageList;
		END;

	CONST 		
		nameTag* = 0;
		dependsTag* = 1;
		descriptionEnTag* = 2;
		descriptionRuTag* = 3;
		urlTag* = 4;
		labelsTag* = 5;
		
	PROCEDURE FindPackageBy* (c: PackageList; tag: INTEGER; tagVal: TextMappers.String): Package;
	BEGIN
		WHILE (c # NIL) & (c.package.data[tag] # tagVal) DO c := c.next; END;
		IF c = NIL THEN RETURN NIL ELSE RETURN c.package; END;
	END FindPackageBy;

	PROCEDURE AddToList* (VAR list: PackageList; p: Package; predicateTag: INTEGER);
		VAR q: PackageList;
	BEGIN
		IF FindPackageBy(list, predicateTag, p.data[predicateTag]) = NIL THEN
			NEW(q);
			q.package := p;
			q.next := list;
			list := q;
		END;
	END AddToList;

	PROCEDURE AddToListSorted* (VAR list: PackageList; p: Package; sortTag: INTEGER);
		VAR q, prev, new: PackageList;
	BEGIN
		q := list;
		WHILE (q # NIL) & (q.package.data[sortTag] < p.data[sortTag]) DO
			prev := q;
			q := q.next;
		END;
		IF (q = NIL) OR (p.data[sortTag] < q.package.data[sortTag]) THEN
			NEW(new);
			new.package := p;
			new.next := q;
			IF prev # NIL THEN
				(* add package to the tail *)
				prev.next := new;
			ELSE
				(* single item list *)
				list := new;
			END;
		END;

	END AddToListSorted;

	PROCEDURE PopFromList* (VAR list: PackageList): Package;
		VAR p: Package;
	BEGIN
		IF list = NIL THEN RETURN NIL
		ELSE
			p := list.package;
			list := list.next;
			RETURN p;
		END;
	END PopFromList;

END PaketModel.

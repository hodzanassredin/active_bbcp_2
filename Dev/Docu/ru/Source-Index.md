# Индекс исходников Dev

Снимок текущего `Dev/Mod`: 2026-09-27. Номера строк служат ориентиром и меняются при редактировании. Учтены действующие объявления до `END ModuleName.`; закомментированные процедуры и примеры после конца модуля исключены. Это навигационный индекс, не замена семантических контрактов в статьях.

[Карта Dev](Sys-Map.md) · [Архитектура](Compiler-Architecture.md) · [DevCPT](CPT.md)

## DevAlienTool

[Исходник](../../Mod/AlienTool.cp) · [Документация](AlienTool.md)

IMPORT: `Services, Ports, Stores, Models, Views, Controllers, Properties, Dialog, Containers, Documents, TextModels, TextMappers, TextViews, StdFolds`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Analyze` | 109 |

## DevAnalyzer

[Исходник](../../Mod/Analyzer.cp) · [Документация](Analyzer.md)

IMPORT: `TextMappers, TextModels, TextViews, DevMarkers, Stores, Models, Dialog, StdLog, CPT := DevCPT, CPS := DevCPS, CPM := DevCPM, CPB := DevCPB, StdRegistry, Utf`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Module` | 2023 |
| `Analyze` | 2169 |
| `ResetOptions` | 2182 |
| `SaveOptions` | 2192 |
| `LoadOptions` | 2203 |

## DevBootLinker

[Исходник](../../Mod/BootLinker.cp) · [Документация](BootLinker.md)

IMPORT: `Kernel, Librarian, Files, Dialog, Log, TextMappers, DevCommanders`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Link` | 232 |
| `SetLoaderName` | 306 |

## DevBrowser

[Исходник](../../Mod/Browser.cp) · [Документация](Browser.md)

IMPORT: `Kernel, Files, Fonts, Dates, Ports, Stores, Views, Properties, Dialog, Documents, TextModels, TextMappers, TextRulers, TextViews, TextControllers, StdDialog, StdFolds, DevReferences, DevCPM, DevCPT, DevDecoder386, StdRegistry, Utf`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `ShowInterface` | 1077 |
| `ImportSymFile` | 1100 |
| `ShowCodeFile` | 1228 |
| `ImportCodeFile` | 1246 |
| `SaveOptions` | 1255 |
| `SaveGuard` | 1262 |
| `Init` | 1269 |

## DevCPB

[Исходник](../../Mod/CPB.cp) · [Документация](CPB.md)

IMPORT: `DevCPT, DevCPM`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `NewLeaf` | 94 |
| `Construct` | 116 |
| `Link` | 123 |
| `NewBoolConst` | 140 |
| `OptIf` | 147 |
| `Nil` | 169 |
| `EmptySet` | 176 |
| `GetTempVar` | 192 |
| `NewIntConst` | 453 |
| `NewLargeIntConst` | 460 |
| `NewRealConst` | 467 |
| `NewString` | 477 |
| `CheckParameters` | 663 |
| `CheckNewParamPair` | 682 |
| `DeRef` | 699 |
| `StrDeref` | 714 |
| `Index` | 728 |
| `Field` | 744 |
| `TypTest` | 754 |
| `In` | 788 |
| `MOp` | 807 |
| `Op` | 982 |
| `SetRange` | 1131 |
| `SetElem` | 1159 |
| `CheckAssign` | 1178 |
| `StPar0` | 1282 |
| `StPar1` | 1545 |
| `StParN` | 1745 |
| `StFct` | 1789 |
| `PrepCall` | 1872 |
| `Param` | 1888 |
| `StaticLink` | 1930 |
| `Call` | 1941 |
| `Enter` | 1955 |
| `Return` | 1962 |
| `Assign` | 1975 |
| `Inittd` | 1988 |
| `CheckBuffering` | 2098 |
| `CheckVarParBuffering` | 2140 |
| `OptimizeCase` | 2188 |

## DevCPC486

[Исходник](../../Mod/CPC486.cp) · [Документация](CPC486.md)

IMPORT: `SYSTEM, DevCPM, DevCPT, DevCPE, DevCPL486`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Init` | 119 |
| `setCC` | 142 |
| `StackAlloc` | 160 |
| `CheckAv` | 215 |
| `SetReg` | 191 |
| `CheckReg` | 196 |
| `Free` | 283 |
| `Fits` | 304 |
| `Pop` | 314 |
| `Load` | 329 |
| `Push` | 362 |
| `Assert` | 383 |
| `CopyReg` | 484 |
| `GetAdr` | 491 |
| `Tag` | 546 |
| `ContainsIPtrs` | 577 |
| `GuidFromString` | 596 |
| `IPAddRef` | 605 |
| `IPRelease` | 630 |
| `Prepare` | 652 |
| `Field` | 693 |
| `DeRef` | 698 |
| `Index` | 710 |
| `TypTest` | 771 |
| `ShortTypTest` | 799 |
| `Convert` | 1027 |
| `LoadCond` | 1041 |
| `IntDOp` | 1064 |
| `LargeInc` | 1113 |
| `FloatDOp` | 1124 |
| `IntMOp` | 1192 |
| `FloatMOp` | 1214 |
| `MakeSet` | 1222 |
| `MakeCond` | 1251 |
| `Not` | 1263 |
| `Odd` | 1268 |
| `In` | 1282 |
| `Shift` | 1289 |
| `DivMod` | 1320 |
| `Mem` | 1331 |
| `SysMove` | 1339 |
| `Len` | 1349 |
| `CmpString` | 1368 |
| `Assign` | 1416 |
| `ArrayLen` | 1441 |
| `AddCopy` | 1469 |
| `Copy` | 1479 |
| `StrLen` | 1520 |
| `MulDim` | 1531 |
| `SetDim` | 1549 |
| `SysNew` | 1564 |
| `New` | 1569 |
| `Param` | 1647 |
| `Result` | 1763 |
| `PrepCall` | 1792 |
| `PreCcall16` | 1802 |
| `PostCcall16` | 1820 |
| `CCallParSize` | 1830 |
| `Call` | 1849 |
| `CopyDynArray` | 1943 |
| `Enter` | 2203 |
| `Exit` | 2265 |
| `InstallStackAlloc` | 2312 |
| `Trap` | 2349 |
| `Jump` | 2354 |
| `JumpT` | 2359 |
| `JumpF` | 2364 |
| `CaseTableJump` | 2369 |
| `CaseJump` | 2379 |

## DevCPE

[Исходник](../../Mod/CPE.cp) · [Документация](CPE.md)

IMPORT: `SYSTEM, Dates, DevCPM, DevCPT`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `GetLongWords` | 98 |
| `GetRealWord` | 121 |
| `GetRealWords` | 127 |
| `AllocConst` | 140 |
| `AllocTypDesc` | 177 |
| `PutByte` | 187 |
| `PutShort` | 192 |
| `PutWord` | 201 |
| `ThisByte` | 212 |
| `ThisShort` | 217 |
| `ThisWord` | 226 |
| `GenByte` | 235 |
| `GenShort` | 248 |
| `GenWord` | 257 |
| `OffsetLink` | 279 |
| `TypeObj` | 309 |
| `OutRefName` | 396 |
| `OutRefs` | 401 |
| `OutSourceRef` | 438 |
| `OutGuid` | 481 |
| `OutHeader` | 946 |
| `OutCode` | 977 |
| `Init` | 1069 |
| `Close` | 1090 |

## DevCPH

[Исходник](../../Mod/CPH.cp) · [Документация](CPH.md)

IMPORT: `DevCPT`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `UseCalls` | 215 |
| `UseReals` | 244 |

## DevCPL486

[Исходник](../../Mod/CPL486.cp) · [Документация](CPL486.md)

IMPORT: `DevCPM, DevCPT, DevCPE`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `MakeReg` | 105 |
| `MakeConst` | 111 |
| `AllocConst` | 116 |
| `BegStat` | 137 |
| `EndStat` | 141 |
| `SetLabel` | 147 |
| `CaseEntry` | 179 |
| `GenMove` | 281 |
| `GenExtMove` | 346 |
| `GenSignExt` | 358 |
| `GenLoadAdr` | 369 |
| `GenPush` | 385 |
| `GenPop` | 400 |
| `GenAdd` | 436 |
| `GenAddC` | 464 |
| `GenSub` | 476 |
| `GenSubC` | 504 |
| `GenComp` | 516 |
| `GenAnd` | 529 |
| `GenOr` | 538 |
| `GenXor` | 547 |
| `GenTest` | 556 |
| `GenNeg` | 573 |
| `GenNot` | 581 |
| `GenMul` | 588 |
| `GenDiv` | 637 |
| `GenShiftOp` | 688 |
| `GenBitOp` | 710 |
| `GenSetCC` | 723 |
| `GenFLoad` | 730 |
| `GenFStore` | 743 |
| `GenFDOp` | 757 |
| `GenFMOp` | 768 |
| `GenJump` | 775 |
| `GenExtJump` | 797 |
| `GenIndJump` | 805 |
| `GenCaseJump` | 810 |
| `GenCaseEntry` | 837 |
| `GenCall` | 844 |
| `GenAssert` | 860 |
| `GenReturn` | 875 |
| `GenBlockMove` | 910 |
| `GenBlockStore` | 929 |
| `GenBlockComp` | 948 |
| `GenStringMove` | 957 |
| `GenStringComp` | 987 |
| `GenStringLength` | 1002 |
| `GenStrStore` | 1010 |
| `GenCode` | 1026 |
| `Init` | 1033 |
| `Close` | 1040 |

## DevCPM

[Исходник](../../Mod/CPM.cp) · [Документация](CPM.md)

IMPORT: `SYSTEM, Files, Stores, Models, TextModels, StdLog, DevMarkers, Dialog, Utf, Librarian`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `err` | 482 |
| `Init` | 135 |
| `Close` | 143 |
| `Get` | 151 |
| `LineOf` | 156 |
| `ValidGuid` | 200 |
| `GetProcSysFlag` | 219 |
| `GetVarParSysFlag` | 244 |
| `GetRecordSysFlag` | 264 |
| `GetArraySysFlag` | 292 |
| `GetPointerSysFlag` | 308 |
| `GetProcTypSysFlag` | 324 |
| `PropagateRecordSysFlag` | 332 |
| `PropagateRecPtrSysFlag` | 343 |
| `PropagateArrPtrSysFlag` | 362 |
| `PutUtf8` | 380 |
| `GetUtf8` | 395 |
| `LogW` | 414 |
| `LogWStr` | 419 |
| `LogWPar` | 424 |
| `LogWNum` | 433 |
| `LogWLn` | 438 |
| `Mark` | 464 |
| `InsertMarks` | 487 |
| `FPrint` | 531 |
| `FPrintSet` | 544 |
| `FPrintReal` | 548 |
| `FPrintLReal` | 552 |
| `SymRCh` | 644 |
| `SymRInt` | 651 |
| `SymRSet` | 657 |
| `SymRReal` | 662 |
| `SymRLReal` | 667 |
| `eofSF` | 672 |
| `OldSym` | 677 |
| `CloseOldSym` | 697 |
| `SymWCh` | 705 |
| `SymWInt` | 711 |
| `SymWSet` | 716 |
| `SymWReal` | 721 |
| `SymWLReal` | 726 |
| `SymReset` | 731 |
| `NewSym` | 736 |
| `RegisterNewSym` | 750 |
| `DeleteNewSym` | 759 |
| `ObjW` | 767 |
| `ObjWNum` | 772 |
| `ObjWLInt` | 783 |
| `ObjWBytes` | 789 |
| `ObjLen` | 797 |
| `ObjSet` | 802 |
| `NewObj` | 807 |
| `RegisterObj` | 822 |
| `DeleteObj` | 831 |
| `Ask` | 836 |

## DevCPP

[Исходник](../../Mod/CPP.cp) · [Документация](CPP.md)

IMPORT: `DevCPM, DevCPT, DevCPB, DevCPS`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Module` | 1615 |

## DevCPS

[Исходник](../../Mod/CPS.cp) · [Документация](CPS.md)

IMPORT: `SYSTEM, Math, Strings, Utf, DevCPM, DevCPT`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `StdKeywords` | 94 |
| `Get` | 313 |
| `Init` | 381 |

## DevCPT

[Исходник](../../Mod/CPT.cp) · [Документация](CPT.md)

IMPORT: `DevCPM, Utf`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `NewConst` | 198 |
| `NewObj` | 203 |
| `NewStr` | 208 |
| `NewNode` | 214 |
| `NewName` | 220 |
| `OpenScope` | 229 |
| `CloseScope` | 237 |
| `Init` | 241 |
| `Open` | 251 |
| `Close` | 256 |
| `SameType` | 264 |
| `EqualType` | 269 |
| `Extends` | 293 |
| `Includes` | 303 |
| `FindImport` | 317 |
| `Find` | 354 |
| `FindField` | 387 |
| `FindBaseField` | 393 |
| `Insert` | 466 |
| `InsertField` | 492 |
| `IdFPrint` | 525 |
| `FPrintSign` | 512 |
| `FPrintStr` | 545 |
| `FPrintObj` | 649 |
| `FPrintErr` | 708 |
| `Import` | 1125 |
| `Export` | 1489 |
| `EnterBoolConst` | 1538 |
| `EnterRealConst` | 1545 |
| `EnterTyp` | 1551 |
| `EnterProc` | 1561 |
| `EnterAttr` | 1567 |
| `InitBasic` | 1612 |
| `PostSetup` | 1683 |
| `InitDefault` | 1705 |

## DevCPV486

[Исходник](../../Mod/CPV486.cp) · [Документация](CPV486.md)

IMPORT: `SYSTEM, DevCPM, DevCPT, DevCPE, DevCPH, DevCPL486, DevCPC486`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Init` | 107 |
| `Close` | 116 |
| `TypeSize` | 272 |
| `Allocate` | 546 |
| `Module` | 1758 |

## DevChmod__Fbsd

[Исходник](../../Mod/Chmod__Fbsd.cp) · [Документация](Chmod__Fbsd.md)

IMPORT: `Libc := FbsdLibc, Files, DevLnkBase, Utf`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Chmod` | 11 |

## DevChmod__Lin

[Исходник](../../Mod/Chmod__Lin.cp) · [Документация](Chmod__Lin.md)

IMPORT: `Libc := LinLibc, Files, DevLnkBase, Utf`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Chmod` | 11 |

## DevChmod__Nbsd

[Исходник](../../Mod/Chmod__Nbsd.cp) · [Документация](Chmod__Nbsd.md)

IMPORT: `Libc := NbsdLibc, Files, DevLnkBase, Utf`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Chmod` | 11 |

## DevChmod__Obsd

[Исходник](../../Mod/Chmod__Obsd.cp) · [Документация](Chmod__Obsd.md)

IMPORT: `Libc := ObsdLibc, Files, DevLnkBase, Utf`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Chmod` | 11 |

## DevCmds

[Исходник](../../Mod/Cmds.cp) · [Документация](Cmds.md)

IMPORT: `Services, Files, Fonts, Ports, Stores, Models, Views, Controllers, Dialog, Containers, Controls, Properties, Documents, TextModels, TextMappers, TextRulers, TextViews, TextControllers, StdDialog, StdCmds, StdMenuTool`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `OpenModuleList` | 21 |
| `OpenFileList` | 80 |
| `FlushResources` | 96 |
| `RevalidateView` | 104 |
| `RevalidateViewGuard` | 114 |
| `SetDefaultButton` | 131 |
| `SetCancelButton` | 154 |
| `ShowControlList` | 189 |

## DevCommanders

[Исходник](../../Mod/Commanders.cp) · [Документация](Commanders.md)

IMPORT: `Kernel, Strings, Fonts, Ports, Stores, Models, Views, Controllers, Properties, Dialog, Controls, TextModels, TextSetters, TextMappers, Services, StdLog`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `New` | 92 |
| `NewEnd` | 93 |
| `Deposit` | 367 |
| `DepositEnd` | 372 |
| `SetDir` | 377 |

## DevCompiler

[Исходник](../../Mod/Compiler.cp) · [Документация](Compiler.md)

IMPORT: `Kernel, Files, Strings, Views, Dialog, Controls, Utf, Librarian, Converters, TextModels, TextMappers, TextViews, TextControllers, StdLog, DevMarkers, DevCommanders, DevSelectors, DevCPM, DevCPT, DevCPB, DevCPP, DevCPE, DevCPV := DevCPV486, DevCPS`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Compile` | 156 |
| `CompileOpt` | 171 |
| `CompileText` | 203 |
| `CompileAndUnload` | 212 |
| `CompileSelection` | 242 |
| `CompileModuleList` | 395 |
| `CompileThis` | 411 |
| `MakeList` | 626 |
| `CompileSubs` | 650 |
| `InitLogMarks` | 673 |

## DevDebug

[Исходник](../../Mod/Debug.cp) · [Документация](Debug.md)

IMPORT: `SYSTEM, Kernel, Strings, Dates, Files, Fonts, Services, Ports, Stores, Converters, Models, Views, Controllers, Properties, Dialog, Containers, Controls, Windows, StdDialog, StdFolds, StdLinks, TextModels, TextMappers, TextControllers, TextViews, TextRulers, StdLog, DevReferences, DevCommanders, Utf, Librarian, DevCPS`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `HeapRefView` | 971 |
| `ShowLoadedModules` | 1000 |
| `UpdateModules` | 1008 |
| `ShowGlobalVariables` | 1022 |
| `UpdateGlobals` | 1034 |
| `ShowHeapObject` | 1051 |
| `ShowViewState` | 1064 |
| `Unload` | 1116 |
| `UnloadModuleList` | 1158 |
| `UnloadThis` | 1168 |
| `Execute` | 1180 |

## DevDecoder386

[Исходник](../../Mod/Decoder386.cp) · [Документация](Decoder386.md)

IMPORT: `SYSTEM, Kernel, Utf, Files, TextMappers, TextRulers, Ports, Strings, Views, TextModels, StdDialog, Dialog, TextViews, StdLinks, TextControllers`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Decode` | 62 |
| `DecodeProc` | 2026 |
| `ShowSrc` | 2059 |

## DevDependencies

[Исходник](../../Mod/Dependencies.cp) · [Документация](Dependencies.md)

IMPORT: `Kernel, Files, Utf, Dialog, TextMappers, TextControllers, StdMenuTool, StdDialog, StdFolds, Strings, Views, Ports, Librarian, Fonts, Properties, Controllers, Stores, StdCmds, Math, TextModels, TextViews, Dates, DevReferences, DevCommanders`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `ExpandClick` | 1008 |
| `CollapseClick` | 1015 |
| `HideClick` | 1022 |
| `ToggleBasicSystemsClick` | 1032 |
| `ShowAllClick` | 1044 |
| `ExpandAllClick` | 1080 |
| `CollapseAllClick` | 1086 |
| `ArrangeClick` | 1092 |
| `CreateToolClick` | 1345 |
| `SelGuard` | 1352 |
| `ModsGuard` | 1364 |
| `SubsGuard` | 1376 |
| `ShowBasicGuard` | 1388 |
| `NewAnalysisClick` | 1450 |
| `Deposit` | 1478 |
| `CreateTool` | 1484 |

## DevHeapSpy

[Исходник](../../Mod/HeapSpy.cp) · [Документация](HeapSpy.md)

IMPORT: `S := SYSTEM, Kernel, Utf, Strings, Ports, Models, Views, Services, Stores, Properties, Dialog, Controllers, Documents, DevDebug`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `ShowHeap` | 609 |
| `GetAnchor` | 617 |
| `ShowAnchor` | 622 |

## DevInspector

[Исходник](../../Mod/Inspector.cp) · [Документация](Inspector.md)

IMPORT: `Kernel, Services, Stores, Views, Controllers, Properties, Containers, Dialog, Controls, Librarian, TextModels, Files, Models, StdDialog, TextViews, TextControllers, Strings`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `InitDialog` | 138 |
| `GetNext` | 145 |
| `Set` | 166 |
| `OptGuard` | 187 |
| `LevelGuard` | 204 |
| `NotifierGuard` | 212 |
| `GuardGuard` | 220 |
| `LabelGuard` | 228 |
| `LinkGuard` | 236 |
| `ControlGuard` | 244 |
| `Notifier` | 249 |
| `LinkSourceGuard` | 419 |
| `LinkSource` | 424 |
| `LabelSourceGuard` | 429 |
| `LabelSource` | 436 |
| `GuardSourceGuard` | 441 |
| `GuardSource` | 446 |
| `NotifierSourceGuard` | 451 |
| `NotifierSource` | 456 |

## DevLinkChk

[Исходник](../../Mod/LinkChk.cp) · [Документация](LinkChk.md)

IMPORT: `Strings, Dialog, Files, Fonts, Ports, Converters, Views, Containers, TextModels, TextMappers, TextViews, TextControllers, StdLinks, StdCmds`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Check` | 331 |
| `Open` | 395 |
| `ListLinks` | 415 |
| `CheckLinks` | 421 |
| `SubsystemGuard` | 427 |
| `CommandGuard` | 432 |
| `SubsystemNotifier` | 448 |

## DevLinker

[Исходник](../../Mod/Linker.cp) · [Документация](Linker.md)

IMPORT: `Kernel, Files, Dates, Dialog, Strings, TextMappers, StdLog, DevCommanders, Utf, Librarian`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Link` | 1454 |
| `LinkExe` | 1460 |
| `LinkDll` | 1466 |
| `LinkDynDll` | 1472 |

## DevLinker1

[Исходник](../../Mod/Linker1.cp) · [Документация](Linker1.md)

IMPORT: `LB := DevLnkBase, Load := DevLnkLoad, WrPe := DevLnkWritePe, WrElf := DevLnkWriteElf, WrElfS := DevLnkWriteElfStatic, Kernel, Files, Log := StdLog, Dialog, DevCommanders, TextMappers`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `LinkPeExe` | 151 |
| `LinkPe` | 158 |
| `LinkPeDll` | 165 |
| `LinkElfStatic` | 171 |
| `LinkElfDll` | 177 |
| `LinkElfExe` | 183 |
| `LinkElf` | 190 |

## DevLnkBase

[Исходник](../../Mod/LnkBase.cp) · [Документация](LnkBase.md)

IMPORT: `Files, Log, Dialog, Kernel, Librarian`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `SetChmodHook` | 138 |
| `Chmod` | 145 |
| `Init` | 153 |
| `Error` | 202 |
| `NewSection` | 208 |
| `RegMod` | 217 |
| `ThisDll` | 233 |
| `ImportProc` | 256 |
| `ExportProc` | 273 |
| `ExportVar` | 286 |
| `InitFixTable` | 300 |
| `FixCode` | 330 |
| `FixRel` | 335 |
| `FixData` | 340 |
| `FixCopy` | 345 |
| `AddReloc` | 363 |
| `DoFixups` | 369 |
| `SortReloc` | 405 |
| `InitStringTable` | 427 |
| `AddName` | 449 |
| `AddHintName` | 461 |
| `AddDllName` | 471 |
| `WriteStringTable` | 483 |
| `Aligned` | 490 |
| `ThisObjFile` | 495 |
| `BeginLinking` | 634 |
| `SetAddr` | 647 |

## DevLnkLoad

[Исходник](../../Mod/LnkLoad.cp) · [Документация](LnkLoad.md)

IMPORT: `LB := DevLnkBase, SYSTEM, Files`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `ThisMod` | 35 |
| `ExportVariable` | 470 |
| `AddModule` | 504 |
| `MainModule` | 510 |
| `ExportModule` | 517 |
| `ExportAll` | 524 |

## DevLnkWriteElf

[Исходник](../../Mod/LnkWriteElf.cp) · [Документация](LnkWriteElf.md)

IMPORT: `Dialog, LB := DevLnkBase, Files, SYSTEM`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Init` | 270 |
| `GetBases` | 337 |
| `WriteOut` | 780 |

## DevLnkWriteElfStatic

[Исходник](../../Mod/LnkWriteElfStatic.cp) · [Документация](LnkWriteElfStatic.md)

IMPORT: `Dialog, LB := DevLnkBase, Files`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `AddReloc` | 89 |
| `Init` | 113 |
| `GetBases` | 136 |
| `WriteOut` | 232 |

## DevLnkWritePe

[Исходник](../../Mod/LnkWritePe.cp) · [Документация](LnkWritePe.md)

IMPORT: `LB := DevLnkBase, Files`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Init` | 86 |
| `GetBases` | 126 |
| `WriteOut` | 454 |

## DevMarkers

[Исходник](../../Mod/Markers.cp) · [Документация](Markers.md)

IMPORT: `Kernel, Files, Stores, Fonts, Ports, Models, Views, Controllers, Properties, Dialog, TextModels, TextSetters, TextViews, TextControllers, TextMappers, Windows`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `InitErr` | 81 |
| `InitMsg` | 88 |
| `SetMode` | 98 |
| `New` | 111 |
| `NewMsg` | 112 |
| `LoadMsg` | 187 |
| `Insert` | 262 |
| `Unmark` | 345 |
| `ToggleCurrent` | 352 |
| `ShowFirstError` | 371 |
| `NextError` | 388 |
| `UnmarkErrors` | 413 |
| `SetDir` | 522 |

## DevMsgSpy

[Исходник](../../Mod/MsgSpy.cp) · [Документация](MsgSpy.md)

IMPORT: `SYSTEM, Strings, DevDebug, Dialog, Stores, Models, Views, Controllers, Properties, Kernel, Ports, TextModels, TextViews, TextMappers, StdDialog, Files, Converters, FormViews, FormModels, Windows, Containers, Services`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `Toggle` | 294 |
| `ToggleGuard` | 312 |
| `Reset` | 326 |
| `Clear` | 332 |
| `OpenDialog` | 354 |

## DevPacker

[Исходник](../../Mod/Packer.cp) · [Документация](Packer.md)

IMPORT: `Kernel, Services, Strings, Files, Dialog, Stores, Views, Librarian, TextModels, TextViews, TextMappers, StdLog, DevCommanders`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `ListFromSub` | 169 |
| `ListLoadedModules` | 208 |
| `PackThis` | 368 |

## DevProfiler

[Исходник](../../Mod/Profiler.cp) · [Документация](Profiler.md)

IMPORT: `Kernel, Strings, Dialog, Utf, Fonts, Ports, Views, TextModels, TextRulers, TextViews, TextControllers, TextMappers, StdLog`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `SetProfilerHook` | 46 |
| `Count` | 51 |
| `StartRecording` | 71 |
| `StopRecording` | 73 |
| `Start` | 91 |
| `Stop` | 136 |
| `Reset` | 148 |
| `SetModuleList` | 155 |
| `Execute` | 210 |
| `ShowProfile` | 268 |
| `SetProfileList` | 323 |
| `StartGuard` | 355 |
| `StopGuard` | 360 |

## DevProfiler__Win

[Исходник](../../Mod/Profiler__Win.cp) · [Документация](Profiler__Win.md)

IMPORT: `WinApi, WinMM, DevProfiler`.

Экспортируемых процедур с собственным именем `*` нет; смотреть тело модуля, переопределения методов или экспортируемые данные.

## DevRBrowser

[Исходник](../../Mod/RBrowser.cp) · [Документация](RBrowser.md)

IMPORT: `Strings, Dialog, Files, Stores, Converters, Fonts, Ports, Views, Containers, TextModels, TextMappers, TextRulers, TextViews, StdLinks, StdFolds, Models`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `ShowRepository` | 324 |
| `Update` | 337 |
| `ShowFiles` | 363 |
| `OpenFile` | 399 |

## DevReferences

[Исходник](../../Mod/References.cp) · [Документация](References.md)

IMPORT: `Strings, Files, Fonts, Models, Views, Dialog, StdDialog, StdApi, Converters, DevCPS, Librarian, TextModels, TextMappers, TextViews, TextControllers`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `ShowText` | 61 |
| `ResolveImportAlias` | 126 |
| `Show` | 147 |
| `ShowSource` | 174 |
| `ShowDocu` | 179 |

## DevSearch

[Исходник](../../Mod/Search.cp) · [Документация](Search.md)

IMPORT: `Kernel, Files, Fonts, Ports, Models, Views, Containers, Dialog, Windows, Strings, TextModels, TextRulers, TextViews, TextControllers, TextMappers, TextCmds, StdLinks`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `SearchInSources` | 216 |
| `SearchInDocu` | 221 |
| `SelectCaseSens` | 244 |
| `SelectCaseInSens` | 249 |
| `Compare` | 316 |

## DevSelectors

[Исходник](../../Mod/Selectors.cp) · [Документация](Selectors.md)

IMPORT: `Ports, Stores, Models, Views, Controllers, Fonts, Properties, TextModels, TextViews, TextSetters`.

| Экспортируемая процедура/метод | Строка |
|---|---:|
| `New` | 43 |
| `Change` | 192 |
| `ChangeTo` | 204 |
| `HandleCtrlMsg` | 271 |
| `Restore` | 300 |
| `InitContext` | 332 |
| `SetDir` | 386 |
| `DepositLeft` | 393 |
| `DepositMiddle` | 398 |
| `DepositRight` | 403 |

## DevSubTool

[Исходник](../../Mod/SubTool.cp) · [Документация](SubTool.md)

IMPORT: `Strings, Files, Fonts, Views, TextModels, TextMappers, TextViews, StdCmds, Dialog, DevCommanders`.

Экспортируемых процедур с собственным именем `*` нет; смотреть тело модуля, переопределения методов или экспортируемые данные.


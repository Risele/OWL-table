FUNCTION_BLOCK StartPermit
VAR_INPUT
Start : BOOL; // Команда пуска
Ready : BOOL; // Привод готов
END_VAR
VAR_OUTPUT
Run : BOOL;
NoRun : BOOL;
END_VAR
Run := Start AND Ready;
NoRun := NOT Run;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 279 926022878 c3f2e1db883ae843930d552c47b2c1d9 2
// LWB1 1/2 eNpNjsFqwkAQhl8lzHkvEU85-gBF7DGEEnUFIcm2m1lBJKD24tWrF18hqKWlVH2Ff9-ok6QtvQwz___PfBPTwJiMFD1yanmobT5nUiuajacUsXVaUcldV4k8L54dP01MZmxJUdxT_UT9iXmuC25kwgE3fKHGFRfUAe7-1W_wiVpAOPo1PnCSxCXAGTe_lfZE
// LWB1 2/2 v4faA-03Eh7pdLrsrIW2rOUpejAjV4hnHEtchG60LtPN7op-PhFHWC04EGSNd6lvfuf3uNJ_WChT2EBeZCWkKqmSb4YDdb4

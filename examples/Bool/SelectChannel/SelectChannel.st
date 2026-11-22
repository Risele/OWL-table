FUNCTION_BLOCK SelectChannel
VAR_INPUT
I1 : BOOL; // Канал 0
I2 : BOOL; // Канал 1
I3 : BOOL; // Канал 2
I4 : BOOL; // Канал 3
I5 : BOOL; // Адрес, младший бит
I6 : BOOL; // Адрес, старший бит
END_VAR
VAR_OUTPUT
Q : BOOL;
NQ : BOOL;
END_VAR
VAR
LogicV3 : BOOL;
LogicInverse0 : BOOL;
LogicV5 : BOOL;
LogicV6 : BOOL;
LogicV7 : BOOL;
LogicV9 : BOOL;
LogicV11 : BOOL;
LogicV12 : BOOL;
LogicV13 : BOOL;
END_VAR
LogicInverse0 := NOT I6;
LogicV3 := I1 AND LogicInverse0;
LogicV5 := I3 AND I6;
LogicV6 := LogicV3 OR LogicV5;
LogicV7 := NOT I5 AND LogicV6;
LogicV9 := I2 AND LogicInverse0;
LogicV11 := I4 AND I6;
LogicV12 := LogicV9 OR LogicV11;
LogicV13 := I5 AND LogicV12;
Q := LogicV7 OR LogicV13;
NQ := NOT Q;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 3992 2833321876 737912fd9e8db16490dd4844b7d0fc6b 3
// LWB1 1/3 eNqdlb1OwzAUhV8l8pyhh7-hI0xZkBBjFSEoRiClCaQOS1Wp7cTIzFMUqUhFhXew3wibIKVJShWfIVGv6-vj4883dyBOsywRobiUiRyqs_vrNJU2noi7m1vRV3khQzFW5a-pHX5IHwt1NcxGI5mqsegPhH7TS_1tn03QswtthaiHB_Xw0IWvemVm-sPMw0B_
// LWB1 2/3 6Y39Y2Ve9Fp_Bvpdr82iMcfMzUIvzaw-Jw7Lbf1uJ3KqkdOKnEJ05F7H7nVSTnyWuZLWmzi_sKNZoWymjVyQF4l0i0zEn0E7LrYXdwbbj132yc7sCXdA_2RiXyb2ZbqZoDVBa7Z1fXyC9gnaJ2if9R378gTNEzRP0DxB8wTNEzRP0DyrU2Lqs3tm87aj825B
// LWB1 3/3 ZzZvHuj69PcJ2idon3Vtpj45nqB5guYJmidonqB5guZZeWb7J6j69NMErdn-LnQ9Wy5zV_8EVZ9-mu1vEds_QdUnxxM0T9A8QfMEzRM0z0qf7Z-g-ydXn6Dr098naJ-gfYL2Wb9LPvfWP3NX_wTdP0H3T64-Qdcn6PpsZsbT-AfHfPz_

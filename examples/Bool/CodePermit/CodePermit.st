FUNCTION_BLOCK CodePermit
VAR_INPUT
I1 : BOOL; // Разряд 0, вес 1
I2 : BOOL; // Разряд 1, вес 2
I3 : BOOL; // Разряд 2, вес 4
I4 : BOOL; // Разряд 3, вес 8
I5 : BOOL; // Разряд 4, вес 16
END_VAR
VAR_OUTPUT
Q : BOOL;
NQ : BOOL;
END_VAR
VAR
LogicV7 : UDINT;
LogicV16 : UDINT;
END_VAR
LogicV7 := 0;
LogicV7.0 := I1;
LogicV7.1 := I2;
LogicV7.2 := I3;
LogicV7.3 := I4;
LogicV7.4 := I5;
LogicV16 := SHR(2531829115, LogicV7);
Q := LogicV16.0;
NQ := NOT Q;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 2004 48648859 28e8521e589b1bb9fcb441b0e4061fad 3
// LWB1 1/3 eNqdVEFOwzAQ_Eq0Zx8ybUGoRzjlguBcRUhQI1Vq6pI6XKpK8AK-wBMQAvEN50esCUpxlIisD4myHs_OjtebBZ0bsyZFF2apr3RZrCypPd3fLmluy0or2tnm68DLq822sjd3pij0xu5oviD36t7cV_1Uv7iPJFWJe3ef9XMCzhhAaKFJF5q00KwLTVvorAvN
// LWB1 2/3 jlqnlKumtJ-SMi-eeZls6l8-a3bS7HnUpdVsjS6vedVUlkkc-aCs1trz9_Trj9fpb96Uo_DhlA-8C-TPZoCFKJbfiT5W-p8WorRCPYkvRPmSa_Wy0jG-jlVK-wWRFqK0EKUVnuPYu9HLGt0viO-hXCvsmcSXjNWdL0TNF6LmC-J-ybQgZiGK1Z0veb8g9iVj
// LWB1 3/3 YYglmi-I_4cQ30OIzxADFeaH_BuekiBw

FUNCTION_BLOCK OddParity
VAR_INPUT
I1 : BOOL; // Сигнал 1
I2 : BOOL; // Сигнал 2
I3 : BOOL; // Сигнал 3
I4 : BOOL; // Сигнал 4
I5 : BOOL; // Сигнал 5
END_VAR
VAR_OUTPUT
Q : BOOL;
NQ : BOOL;
END_VAR
Q := I1 XOR I2 XOR I3 XOR I4 XOR I5;
NQ := NOT Q;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 1952 1287301287 928c04bed58ca23ed5d2d125f1eba9b7 3
// LWB1 1/3 eNqVlE1OwzAQha-CZp2FH5RNl-y8KXRdRQiIK1VqG5o6SKjqYbgBEhskxB18JMakiyRg8FtEikfzzZsfjxdyVddrKeS6qm7umpV_luIgy_tKpr5pXSF73_0d1bzaPrb-9qHebNzW72W6kPAS3sNb-Ayv4eMMGmZgOB8bLsaGydhwKWXR6XzHtzGmjXFsZG30
// LWB1 2/3 tyefJ9d4p3nKbK7WuvUK6SkemnbtIn-QU7Jql35co6fhpyF36mUkFpqgkKLwFxU9QVNIUeY_raFertavlMmpC3QPQWfY7zroun5QJndeoOviMuxnB_oegtYCTSFFmdx5dbrsvAaUYfYL9LzyMxzvF-h5cVqgKaQow-wX6PcQdF0chRRlmP0C_R6Cvoege4hE
// LWB1 3/3 huWx_ALVuwkQ

FUNCTION_BLOCK Forbidden
VAR_INPUT
I1 : BOOL; // Контакт 1
I2 : BOOL; // Контакт 2
I3 : BOOL; // Контакт 3
I4 : BOOL; // Контакт 4
END_VAR
VAR_OUTPUT
Q : BOOL;
NQ : BOOL;
END_VAR
NQ := I1 AND I2 AND I3 AND I4;
Q := NOT NQ;
END_FUNCTION_BLOCK

// Сгенерировано OWL-Tables
// LWB1 Z 1010 1232229764 bd8bf97b8c87a3e614555c8baa44719b 2
// LWB1 1/2 eNqVUbEKwjAU_BV5c4Y-deroIGQRnKUItREKbaNt4lK69DP8CmdBvyH9I19ssWJxeEMg7_LuLsftYKV1BgLWuozTJFEFiBqOcQKhKa0SUJn-1hCcFidr9ged56owFYQ7cFf3dI-udTd379oZktAPNJ9Ciym0hEj08m9Z6YWkp0q_LIfniyqNop_BZkuotob2
// LWB1 2/2 afJDaTPlqTUM3yMcviUDmsZDcmfaQPCx_jCQzfCbyPZAtsfow8mB7BzIzoHsHL0Xtw9k94HsPpDdB7L7QHYfyO7jwwigiZroBZ9BIno

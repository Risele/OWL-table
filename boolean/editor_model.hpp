#pragma once
#include "logic.hpp"
namespace b2f {
constexpr unsigned editor_page_size=256;
J dense_editor_state(const J &);
J rules_editor_state(const J &);
unsigned editor_row_count(const J &);
unsigned editor_index(const J &,unsigned position);
void erase_editor_row(J &,unsigned position);
void swap_editor_inputs(J &,unsigned left,unsigned right);
void move_editor_row(J &,unsigned position,unsigned destination);
J editor_page(const J &,unsigned page);
void edit_dense_cell(J &,unsigned index,unsigned column,const S &);
size_t editor_state_bytes(const J &);
J editor_request(const J &);
}

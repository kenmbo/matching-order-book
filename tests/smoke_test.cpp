#include "lob/project.hpp"

int main() {
  return lob::project_name() == "limit_order_book" ? 0 : 1;
}

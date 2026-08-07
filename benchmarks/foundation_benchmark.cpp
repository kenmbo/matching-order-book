#include "lob/project.hpp"

int main() {
  return lob::project_name().empty() ? 1 : 0;
}

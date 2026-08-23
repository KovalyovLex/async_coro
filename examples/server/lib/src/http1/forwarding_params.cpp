#include <server/http1/forwarding_params.h>

namespace server::http1 {

std::string forwarding_params::format_proxy_element(std::string_view existing_header) const {
  using namespace std::string_view_literals;

  // Check what we need to include
  const bool has_existing = !existing_header.empty();
  const bool has_new_by = !by.empty();
  const bool has_new_for = !for_addr.empty();
  const bool has_new_proto = !proto.empty();
  const bool has_new_params = has_new_by || has_new_for || has_new_proto;

  // Estimate total size upfront for single allocation
  size_t estimated_size = 0;

  if (has_existing) {
    estimated_size += existing_header.size();
    if (has_new_params) {
      estimated_size += ", "sv.size();  // ", " separator between proxy elements
    }
  }

  if (has_new_by) {
    estimated_size += "by="sv.size() + by.size();
  }
  if (has_new_for) {
    estimated_size += ";for="sv.size() + for_addr.size();
  }
  if (has_new_proto) {
    estimated_size += ";proto="sv.size() + proto.size();
  }

  // Single allocation: reserve in new_element
  std::string new_element;
  new_element.reserve(estimated_size);

  // Build entire result into new_element (reuse single allocation)
  if (has_existing) {
    new_element += existing_header;
    if (has_new_params) {
      new_element += ", ";  // RFC 7239: comma separates proxy elements
    }
  }

  // Append new parameters with semicolons within the proxy element
  if (has_new_by) {
    new_element += "by="sv;
    new_element.append(by);
  }

  if (has_new_for) {
    if (has_new_by) {
      new_element += ";"sv;  // Semicolon separates parameters within element
    }
    new_element += "for="sv;
    new_element.append(for_addr);
  }

  if (has_new_proto) {
    if (has_new_by || has_new_for) {
      new_element += ";"sv;  // Semicolon separates parameters within element
    }
    new_element += "proto="sv;
    new_element.append(proto);
  }

  return new_element;
}

}  // namespace server::http1

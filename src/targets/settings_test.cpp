#include "local/settings.h"

int
main(int argc, char **argv)
{
  {
    auto settings =
      zoo::local::safe_load_settings("/tmp/.config/zoo", "settings_test.cfg");
      
    settings->clear();

    // Initial default value
    const auto r = settings->get_or_default("test.key", "default_val") ;
    printf("r: '%s'\n", r.data());
    assert(r == "default_val");

    // New user-defined value
    settings->set("test.key", "other");
    assert(settings->get_or_default("test.key", "default_val") == "other");

    // Back to the user defined value
    settings->erase("test.key");
    assert(settings->get_or_default("test.key", "default_val") == "default_val");
  }

  return 0;
}
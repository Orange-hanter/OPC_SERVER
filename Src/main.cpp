#include "app/application.hpp"
#include "app/cli_options.hpp"

int main(int argc, char const* argv[]) {
    const auto options = opc::app::parse_cli(argc, argv);
    opc::app::Application application;
    if (!application.init(options)) {
        return (options.help || options.version) ? 0 : 2;
    }
    return application.run();
}

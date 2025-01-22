/**
 * wg-keepalive - A simple keepalive tool for WireGuard interfaces
 * 
 * MIT License (c) 2025, Tomoatsu Shimada
 */
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#include <filesystem>
#include <memory>
#include <thread>

#include <argparse/argparse.hpp>
#include <iniparser/iniparser.h>
#include <spdlog/spdlog.h>

std::vector<std::string> split_by_tab(const std::string& str) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, '\t')) {
        tokens.push_back(token);
    }
    return tokens;
}

uint64_t get_rxbytes(const std::string &interface)
{
    std::vector<const char*> command = {
        "wg",
        "show",
        interface.c_str(),
        "dump",
        NULL
    };
    // use fork and exec to run the command
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        throw std::runtime_error("pipe failed");
    }
    auto pid = fork();
    if (pid == -1) {
        // close the pipe before throwing
        close(pipefd[0]);
        close(pipefd[1]);
        throw std::runtime_error("fork failed");
    }
    if (pid == 0) {
        // child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execvp(command[0], const_cast<char* const*>(command.data()));
        // execvp only returns if an error occurred
        std::cerr << "execvp failed: " << strerror(errno) << std::endl;
        _exit(1);
    }
    // parent process
    close(pipefd[1]);
    std::string output;
    char buffer[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, n);
    }
    close(pipefd[0]);
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        throw std::runtime_error("wg command failed");
    }
    // parse the output
    uint64_t rxbytes = 0;
    auto splitted = split_by_tab(output);
    if (splitted.size() < 10) {
        throw std::runtime_error("unexpected output from wg command");
    }
    //else
    return std::stoull(splitted[8]);
}

void keepalive(const std::string &interface, std::shared_ptr<dictionary> config)
{
    auto interval = iniparser_getint(config.get(), ":interval", 60);
    auto timeout = iniparser_getint(config.get(), ":timeout", 300);
    auto pre_restart_command = iniparser_getstring(config.get(), ":pre_restart_command", nullptr);
    auto restart_command = iniparser_getstring(config.get(), ":restart_command", "systemctl restart wg-quick\\@$WG_INTERFACE");
    auto post_restart_command = iniparser_getstring(config.get(), ":post_restart_command", nullptr);

    spdlog::info("Starting keepalive for {} with interval={}, timeout={}", interface, interval, timeout);

    setenv("WG_INTERFACE", interface.c_str(), 1);

    auto last_time = std::chrono::system_clock::now();
    uint64_t last_rxbytes = 0;
    while (true) {
        auto rxbytes = get_rxbytes(interface);
        if (rxbytes != last_rxbytes) {
            spdlog::debug("rxbytes changed from {} to {}", last_rxbytes, rxbytes);
            last_time = std::chrono::system_clock::now();
            last_rxbytes = rxbytes;
        } else {
            spdlog::debug("rxbytes unchanged at {}", rxbytes);
            // check if the timeout has been reached
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_time).count();
            if (elapsed >= timeout) {
                spdlog::warn("Timeout reached, restarting interface");
                // run the restart command
                if (pre_restart_command != nullptr) {
                    spdlog::info("Running pre-restart command: {}", pre_restart_command);
                    system(pre_restart_command);
                }
                system(restart_command);
                if (post_restart_command != nullptr) {
                    spdlog::info("Running post-restart command: {}", post_restart_command);
                    system(post_restart_command);
                }
                last_time = now;
                last_rxbytes = 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(interval));
    }
}

int main(int argc, char **argv)
{
    argparse::ArgumentParser program("wg-keepalive");
    program.add_argument("interface")
        .help("WireGuard interface name")
        .required();
    program.add_argument("--config-dir", "-d")
        .help("Configuration directory")
        .default_value("/etc/wg-keepalive");
    program.add_argument("--loglevel")
        .help("Log level")
        .default_value("info");
    program.add_argument("--no-log-timestamp")
        .help("Disable log timestamp")
        .default_value(false)
        .implicit_value(true);
    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cout << program;
        return 1;
    }
    auto config_dir = program.get<std::string>("--config-dir");

    spdlog::set_level(spdlog::level::from_str(program.get<std::string>("--loglevel")));
    if (program.get<bool>("--no-log-timestamp")) {
        spdlog::set_pattern("%^[%l]%$ %v");
    }

    auto interface = program.get<std::string>("interface");
    try {
        // find corresponding configuration file
        dictionary* config = nullptr;
        auto conf_file = std::filesystem::path(config_dir) / (interface + ".conf");
        if (std::filesystem::exists(conf_file)) {
            config = iniparser_load(conf_file.c_str());
        }
        if (config == nullptr) {
            // empty configuration
            config = iniparser_load("/dev/null");
        }
        std::shared_ptr<dictionary> config_autodelete(config, iniparser_freedict);
        keepalive(interface, config_autodelete);
    } catch (const std::exception &err) {
        spdlog::error("Error: {}", err.what());
        return 1;
    }

    return 0;
}
#include "nixd-config.h"

#include "lspserver/Connection.h"
#include "lspserver/LSPServer.h"
#include "lspserver/Logger.h"

#include "nixd/Server/Server.h"

#include <nix/eval.hh>
#include <nix/shared.hh>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/CommandLine.h>

// Fix build on macOS. See
// https://github.com/nix-community/nixd/actions/runs/5208659631/jobs/9397498730?pr=90
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define BOOST_STACKTRACE_USE_BACKTRACE
#include <boost/filesystem.hpp>
#include <boost/stacktrace.hpp>

#include <filesystem>
#include <fstream>

#include <csignal>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

using lspserver::JSONStreamStyle;
using lspserver::Logger;

namespace nixd {

void printReportInfo() {
  std::cerr
      << "Please file an issue at https://github.com/nix-community/nixd/issues/"
      << "\n"
      << "nixd version: " << NIXD_VERSION
#ifdef NIXD_VCS_TAG
      << " " << NIXD_VCS_TAG
#endif
      << std::endl;
}

void sigHandler(int Signum) {
  ::signal(Signum, SIG_DFL);
  // POSIX say we can use `read` and `write` syscall, this is async-signal-safe
  printReportInfo();
  std::cerr << boost::stacktrace::stacktrace() << "\n";
  ::raise(SIGABRT);
}

void registerSigHanlder() {
  signal(SIGSEGV, sigHandler);
  signal(SIGABRT, sigHandler);
}

} // namespace nixd

using namespace llvm::cl;

OptionCategory Misc("miscellaneous options");

const OptionCategory *NixdCatogories[] = {&Misc};

opt<JSONStreamStyle> InputStyle{
    "input-style",
    desc("Input JSON stream encoding"),
    values(
        clEnumValN(JSONStreamStyle::Standard, "standard", "usual LSP protocol"),
        clEnumValN(JSONStreamStyle::Delimited, "delimited",
                   "messages delimited by `// -----` lines, "
                   "with // comment support")),
    init(JSONStreamStyle::Standard),
    cat(Misc),
    Hidden,
};
opt<bool> LitTest{"lit-test",
                  desc("Abbreviation for -input-style=delimited -pretty "
                       "-log=verbose -wait-worker. "
                       "Intended to simplify lit tests"),
                  init(false), cat(Misc)};
opt<Logger::Level> LogLevel{
    "log", desc("Verbosity of log messages written to stderr"),
    values(
        clEnumValN(Logger::Level::Error, "error", "Error messages only"),
        clEnumValN(Logger::Level::Info, "info", "High level execution tracing"),
        clEnumValN(Logger::Level::Debug, "debug", "Debugging details"),
        clEnumValN(Logger::Level::Verbose, "verbose", "Low level details")),
    init(Logger::Level::Info), cat(Misc)};
opt<bool> PrettyPrint{"pretty", desc("Pretty-print JSON output"), init(false),
                      cat(Misc)};

opt<bool> WaitWorker{"wait-worker",
                     desc("wait all response from workers, instead of having "
                          "any timeout logic"),
                     init(false), cat(Misc)};

int main(int argc, char *argv[]) {
  using namespace lspserver;
#ifdef __linux__
  prctl(PR_SET_PDEATHSIG, SIGHUP);
#endif
  nixd::registerSigHanlder();
  const char *FlagsEnvVar = "NIXD_FLAGS";
  HideUnrelatedOptions(NixdCatogories);
  ParseCommandLineOptions(argc, argv, "nixd language server", nullptr,
                          FlagsEnvVar);

  if (LitTest) {
    InputStyle = JSONStreamStyle::Delimited;
    LogLevel = Logger::Level::Verbose;
    PrettyPrint = true;
    if (!WaitWorker)
      WaitWorker = true;
  }

  StreamLogger Logger(llvm::errs(), LogLevel);
  lspserver::LoggingSession Session(Logger);

#ifdef NIXD_VCS_TAG
  lspserver::log("nixd {0} started", NIXD_VCS_TAG);
#else
  lspserver::log("nixd {0} started", NIXD_VERSION);
#endif
  nixd::Server Server{
      std::make_unique<lspserver::InboundPort>(STDIN_FILENO, InputStyle),
      std::make_unique<lspserver::OutboundPort>(PrettyPrint), WaitWorker};
  Server.run();
  return 0;
}

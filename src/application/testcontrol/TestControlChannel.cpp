#include "TestControlChannel.h"

#include <future>
#include <iostream>

namespace testcontrol
{
TestControlChannel::TestControlChannel(TestControlTarget& target)
    : juce::Thread("test-control"), target_(target)
{
    startThread();
}

TestControlChannel::~TestControlChannel()
{
    // stdin may be blocked in a read that nothing can interrupt, so this does
    // not wait indefinitely. The harness closes the pipe or sends `quit`; a
    // bounded wait keeps a stuck reader from hanging shutdown.
    stopThread(2000);
}

void TestControlChannel::run()
{
    TestControlSession session{target_};

    std::string line;

    while (!threadShouldExit() && std::getline(std::cin, line))
    {
        // Each command runs on the message thread, and this thread waits for
        // it. Handling one command at a time is what lets a reply mean "this
        // has happened" rather than "this has been queued".
        std::promise<std::string> promise;
        std::future<std::string> reply = promise.get_future();

        juce::MessageManager::callAsync(
            [&session, line, &promise]() mutable
            {
                std::string rendered;

                try
                {
                    rendered = renderResponse(session.handleLine(line));
                }
                catch (const std::exception& error)
                {
                    // A throw here would otherwise leave the reader waiting on
                    // a promise nobody fulfils, hanging the harness rather than
                    // failing the command.
                    Response failure;
                    failure.success = false;
                    failure.error = std::string{"unhandled error: "} + error.what();
                    rendered = renderResponse(failure);
                }

                promise.set_value(std::move(rendered));
            });

        std::cout << reply.get() << std::flush;

        if (session.isFinished())
        {
            break;
        }
    }
}
} // namespace testcontrol

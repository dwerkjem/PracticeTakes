#include "MidiInputService.h"

#include "MidiMessageTranslation.h"
#include "MidiRecoveryPolicy.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

MidiInputService::MidiInputService()
{
    startTimerHz(serviceRefreshRateHz);

    // A change to the device list is noted here and acted on once the list has
    // been quiet for `deviceListSettleTicks` -- never in this callback.
    //
    // Reacting at once was tried, and unplugging a real USB keyboard with the
    // application open then hung the kernel: its port deletion
    // (snd_seq_midisynth_remove -> port_delete) waited forever on a reference
    // to the keyboard's port, and USB stopped enumerating until a reboot. The
    // same unplug without the application did not hang. JUCE raises this from
    // ALSA's announcement port while the port is still being torn down, and
    // closing the input here unsubscribed from it mid-teardown -- the likeliest
    // trigger, and the one thing the application can stop doing. Waiting until
    // the list settles lets the kernel finish first. A replug inside the window restarts the wait,
    // and the settled reconcile re-opens the keyboard, so a quick replug still recovers -- the case
    // the scan alone missed. JUCE delivers this on the message thread, as the timer runs.
    deviceListConnection = juce::MidiDeviceListConnection::make(
        [this]
        {
            deviceListChangePending = true;
            ticksUntilNextScan = deviceListSettleTicks;
        });
}

MidiInputService::~MidiInputService()
{
    deviceListConnection.reset();
    stopTimer();

    // Stop the device before anything it writes into goes away. `MidiInput`'s
    // destructor stops its thread, so by the time this returns no callback is
    // in flight.
    const juce::ScopedLock lock(deviceLock);
    openInput.reset();
}

void MidiInputService::addConsumer(Consumer* consumer)
{
    if (consumer == nullptr)
    {
        return;
    }

    const juce::ScopedLock lock(consumerLock);

    if (findConsumer(consumer) != nullptr)
    {
        return;
    }

    for (ConsumerSlot& slot : consumers)
    {
        if (slot.consumer == nullptr)
        {
            slot.fifo.reset();
            slot.consumer = consumer;
            slot.active.store(true, std::memory_order_release);

            return;
        }
    }
}

void MidiInputService::removeConsumer(Consumer* consumer)
{
    const juce::ScopedLock lock(consumerLock);

    if (ConsumerSlot* slot = findConsumer(consumer); slot != nullptr)
    {
        // Deactivate before clearing the pointer: the callback checks `active`
        // and never takes the lock, so this is the order that stops it finding
        // a slot it is about to stop being allowed to write to.
        slot->active.store(false, std::memory_order_release);
        slot->consumer = nullptr;
    }
}

MidiInputService::ConsumerSlot* MidiInputService::findConsumer(Consumer* consumer)
{
    const auto match = std::find_if(
        consumers.begin(), consumers.end(),
        [consumer](const ConsumerSlot& slot) { return slot.consumer == consumer; });

    return match == consumers.end() ? nullptr : &*match;
}

const MidiInputService::ConsumerSlot* MidiInputService::findConsumer(Consumer* consumer) const
{
    const auto match = std::find_if(
        consumers.begin(), consumers.end(),
        [consumer](const ConsumerSlot& slot) { return slot.consumer == consumer; });

    return match == consumers.end() ? nullptr : &*match;
}

std::size_t MidiInputService::availableEvents(Consumer* consumer) const
{
    const juce::ScopedLock lock(consumerLock);
    const ConsumerSlot* slot = findConsumer(consumer);

    return slot == nullptr ? 0 : slot->fifo.available();
}

std::size_t MidiInputService::readEvents(
    Consumer* consumer,
    midi::MidiEvent* destination,
    std::size_t maximumEvents)
{
    const juce::ScopedLock lock(consumerLock);
    ConsumerSlot* slot = findConsumer(consumer);

    return slot == nullptr ? 0 : slot->fifo.pop(destination, maximumEvents);
}

void MidiInputService::discardPendingEvents(Consumer* consumer)
{
    const juce::ScopedLock lock(consumerLock);

    if (ConsumerSlot* slot = findConsumer(consumer); slot != nullptr)
    {
        slot->fifo.discardPending();
    }
}

std::uint64_t MidiInputService::lostEvents(Consumer* consumer) const
{
    const juce::ScopedLock lock(consumerLock);
    const ConsumerSlot* slot = findConsumer(consumer);

    return slot == nullptr ? 0 : slot->fifo.lostEvents();
}

std::vector<MidiInputService::DeviceDescription> MidiInputService::availableDevices() const
{
    std::vector<DeviceDescription> devices;

    for (const juce::MidiDeviceInfo& info : juce::MidiInput::getAvailableDevices())
    {
        devices.push_back({info.identifier, info.name});
    }

    return devices;
}

void MidiInputService::selectDevice(const juce::String& identifier, const juce::String& name)
{
    {
        const juce::ScopedLock lock(deviceLock);

        if (chosenIdentifier == identifier)
        {
            // Keep the name fresh even when the identifier has not moved: a
            // device can be renamed, and the remembered name is what the
            // interface shows while it is away.
            chosenName = name;

            return;
        }

        chosenIdentifier = identifier;
        chosenName = name;
    }

    reconcileDevice();
}

juce::String MidiInputService::selectedDeviceIdentifier() const
{
    const juce::ScopedLock lock(deviceLock);

    return chosenIdentifier;
}

juce::String MidiInputService::selectedDeviceName() const
{
    const juce::ScopedLock lock(deviceLock);

    return chosenName;
}

MidiInputService::ConnectionState MidiInputService::connectionState() const
{
    const juce::ScopedLock lock(deviceLock);

    if (chosenIdentifier.isEmpty())
    {
        return ConnectionState::noDeviceSelected;
    }

    return openInput != nullptr && openIdentifier == chosenIdentifier
               ? ConnectionState::open
               : ConnectionState::selectedDeviceUnavailable;
}

std::uint64_t MidiInputService::receivedEventCount() const noexcept
{
    return eventsReceived.load(std::memory_order_relaxed);
}

std::uint64_t MidiInputService::deviceListGeneration() const noexcept
{
    return deviceListChanges.load(std::memory_order_relaxed);
}

std::vector<MidiRecoveryPolicy::DeviceListing> MidiInputService::presentDevices()
{
    std::vector<MidiRecoveryPolicy::DeviceListing> present;

    for (const juce::MidiDeviceInfo& info : juce::MidiInput::getAvailableDevices())
    {
        present.push_back({info.identifier.toStdString(), info.name.toStdString()});
    }

    return present;
}

std::unique_ptr<juce::MidiInput> MidiInputService::openDevice(const juce::String& identifier)
{
    return juce::MidiInput::openDevice(identifier, this);
}

void MidiInputService::reconcileDevice(bool listChanged)
{
    // Enumerated once, outside the lock, because it is the one step here that
    // calls into the backend.
    juce::String wanted;
    juce::String wantedName;
    juce::String currentlyOpen;

    {
        const juce::ScopedLock lock(deviceLock);
        wanted = chosenIdentifier;
        wantedName = chosenName;
        currentlyOpen = openIdentifier;
    }

    const std::vector<MidiRecoveryPolicy::DeviceListing> present = presentDevices();

    // Which present device is the chosen one -- by identifier while that still
    // carries the chosen name, by the name alone when the identifier has moved.
    const std::optional<std::string> resolved = MidiRecoveryPolicy::resolveSelectedDevice(
        wanted.toStdString(), wantedName.toStdString(), present);
    const juce::String target = resolved.has_value() ? juce::String(*resolved) : juce::String();

    const std::string openId = currentlyOpen.toStdString();
    const bool openIsPresent =
        !openId.empty() &&
        std::any_of(
            present.begin(), present.end(),
            [&openId](const MidiRecoveryPolicy::DeviceListing& device)
            { return device.identifier == openId; });

    const juce::ScopedLock lock(deviceLock);

    // The list changed since the selected device was opened, and it is still
    // present: open it afresh, because the subscription may belong to a port
    // that died with the device before it came back under the same identifier.
    if (MidiRecoveryPolicy::shouldReopenAfterListChange(
            listChanged, openInput != nullptr && openIdentifier == target, resolved.has_value()))
    {
        openInput.reset();
        openIdentifier.clear();
    }

    const bool somethingOpen = openInput != nullptr;

    // What is open is the selection only if it is the device the selection
    // resolved to. An identifier now carrying another device's name resolves to
    // nothing, so a device that took the keyboard's old address is closed
    // rather than read as the keyboard.
    if (MidiRecoveryPolicy::shouldClose(
            somethingOpen, target.isNotEmpty() && openIdentifier == target, openIsPresent))
    {
        openInput.reset();
        openIdentifier.clear();
    }

    const bool selectedIsOpen = openInput != nullptr && openIdentifier == target;

    if (!MidiRecoveryPolicy::shouldAttemptOpen(
            wanted.isNotEmpty(), selectedIsOpen, resolved.has_value()))
    {
        return;
    }

    if (auto device = openDevice(target); device != nullptr)
    {
        device->start();
        openIdentifier = target;
        openInput = std::move(device);

        // The keyboard is where its name now is, so the selection follows it
        // there; saving settings records the new identifier.
        if (chosenIdentifier != target)
        {
            chosenIdentifier = target;
            deviceListChanges.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void MidiInputService::timerCallback()
{
    if (ticksUntilNextScan > 0)
    {
        --ticksUntilNextScan;

        return;
    }

    // Scanned more often while there is nothing open, because that is when a
    // change is being waited for; a connected device is checked only often
    // enough to notice it going away.
    const bool connected = connectionState() == ConnectionState::open;
    ticksUntilNextScan =
        connected ? connectedDeviceScanIntervalTicks : disconnectedDeviceScanIntervalTicks;

    const bool listChanged = std::exchange(deviceListChangePending, false);

    reconcileDevice(listChanged);

    // Only now does the interface hear the list moved, so it re-reads it after
    // the system has settled rather than during a teardown.
    if (listChanged)
    {
        deviceListChanges.fetch_add(1, std::memory_order_relaxed);
    }
}

void MidiInputService::handleIncomingMidiMessage(
    juce::MidiInput* source,
    const juce::MidiMessage& message) noexcept
{
    juce::ignoreUnused(source);

    // Read the message, never copy it. `juce::MidiMessage` owns a heap
    // allocation for any payload over four bytes, so a copy here would be an
    // allocation on a driver callback.
    deliverMessage(
        reinterpret_cast<const std::uint8_t*>(message.getRawData()), message.getRawDataSize(),
        message.getTimeStamp());
}

void MidiInputService::deliverMessage(
    const std::uint8_t* data,
    int size,
    double deviceTimestampSeconds) noexcept
{
    const double received = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const std::optional<midi::MidiEvent> event =
        midi::translate(data, size, deviceTimestampSeconds, received);

    if (!event.has_value())
    {
        // Everything a keyboard sends that this does not model lands here, which
        // is most of what a keyboard sends. Not an error, and nothing is
        // counted for it.
        return;
    }

    eventsReceived.fetch_add(1, std::memory_order_relaxed);

    // No lock: the consumer lock is held by the message thread while
    // registering and draining, and taking it here would be a lock on a driver
    // callback. `active` is what the callback reads instead, and a slot is
    // deactivated before its pointer is cleared.
    for (ConsumerSlot& slot : consumers)
    {
        if (slot.active.load(std::memory_order_acquire))
        {
            slot.fifo.push(*event);
        }
    }
}

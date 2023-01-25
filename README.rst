==========================================================================
Extension for asynchronous transaction service in Linux FireWire subsystem
==========================================================================

2023/01/25 坂本 貴史
Takashi Sakamoto <o-takashi@sakamocchi.jp>

Introduction
============

Linux FireWire subsystem provides service of asynchronous communication to user space application.
Current implementation of the service does not deliver the isochronous cycle at which the request
or response subaction of asynchronous transaction was sent and arrived. It is inconvenient to a
kind of application which attempts to synchronize data from multiple sources by the time stamp.

This repository includes extension for the subsystem so that the isochronous cycle is notified as
data of asynchronous transaction to the user space application and the in-kernel unit driver.

Requirement
===========

- Linux kernel 5.19 or later

Problem of issue
================

In IEEE 1394 (a.k.a FireWire) specification, two types of packet communication services are
defined; isochronous and asynchronous.

The former type of communication service broadcasts isochronous packets 8,000 isochronous cycles
per second. Receivers of the packet do not need to response. For the type of communication service,
up to 80 percent of bus bandwidth is allocated.

The latter type of communication service is transactional. It requires the receiver of request
packet to sent response packet. The transaction consists of two subactions; request and response.
The transmission of packet for the subactions can be postponed when the other node uses
the bus. For the type of communication service, up to 20 percent of bus bandwidth is allocated.

Linux FireWire subsystem uses 1394 Open Host Controller interface (OHCI) hardware to support the
two types of services. 1394 OHCI [#1394ohci]_ is a specification for link layer protocol of IEEE
1394 bus, with additional features to support the transaction and bus management layers. It adopts
PCI interface and DMA engines for software.

In 1394 OHCI specification, Asynchronous Request (AR) context handles both the request and response
subaction of asynchronous transaction. The AR context records the isochronous cycle at which the
packet arrived for the request subaction or the isochronous cycle at which the packet was sent for
the response subaction. Additionally, Asynchronous Transmit (AT) context records the isochronous
cycle at which the packet was sent for the request and response subaction of transaction.

Furthermore, both the AR and AT contexts are used to deliver IEEE 1394 phys packet and the
isochronous cycle for the delivery is also available, except for transmission of phy packet for
ping purpose. In the case of ping phy packet, the round-trip count at 42.195 MHz resolution is
recorded instead of the time stamp at which the phy packet was sent.

The isochronous cycle is expressed in 16 bit time stamp field; higher three bits expresses three
low order bits of second field in the format of ``CYCLE_TIME`` register, and the reset 13 bits
expresses cycle field in the format.

Linux FireWire subsystem provides the service for asynchronous communication to kernel space unit
driver and user space application. For kernel space unit driver, kernel API is provided
[#fw_kernel]_. For user space application, FireWire character device is provided as interface
[#fw_cdev]_. In both ways, the isochronous cycle is not passed as the time stamp of subaction of
transaction.

Current implementation of kernel API for unit driver
====================================================

The interface of kernel API provided by Linux FireWire subsystem is described in
``include/linux/firewire.h``.

The way to retrieve the time stamp from request of AR context is already implemented. The unit driver
reserves the range of address in 1394 OHCI controller to handle request from the other nodes. It's
done by calling ``fw_core_add_address_handler()`` with data of ``fw_address_handler`` structure.
The structure includes a member for callback function which has prototype of
``fw_address_callback_t``.

::

    typedef void (*fw_address_callback_t)(struct fw_card *card,
                                          struct fw_request *request,
                                          int tcode, int destination, int source,
                                          int generation,
                                          unsigned long long offset,
                                          void *data, size_t length,
                                          void *callback_data);

When receiving the request subaction, AR context runs in hardIRQ context, and schedules softIRQ
(``tasklet``). In the softIRQ context, the callback function is called to notify it. The callback
function receives data of ``fw_request`` structure in its second argument. It is an opaque pointer
when the value offset is out of IEC 61883-1 FCP region.

The structure keeps the time stamp, and the call of ``fw_request_get_timestamp()`` returns it. This
function was added to Linux kernel v5.19 at a commit 10def3ce7672 ("firewire: add kernel API to
access packet structure in request structure for AR context"). However, the request argument is
still NULL when the value of offset is in the region. It is inconvenient for the callback
implementation since the time stamp is not available in the case.

Besides, no time stamp is available for outbound asynchronous transaction yet for the unit driver.
The unit drivers calls ``fw_send_request()`` with callback function which has prototype of
``fw_transaction_callback_t``,  then Linux FireWire subsystem queues the request subaction to AT
context, and 1394 OHCI controller sends it to the destination node when the conditions are
satisfied. The destination node sends the response subaction later, then the AR context runs in
hardIRQ context, and it schedules softIRQ (``tasklet``). In the softIRQ context, the callback
function is called to notify the result of request.

::

    typedef void (*fw_transaction_callback_t)(struct fw_card *card, int rcode,
                                              void *data, size_t length,
                                              void *callback_data);

It is inconvenient that the prototype has no room to receive the time stamp for the case. It is
need to add the other prototype with time stamp.

Current implementation of character device for user space application
=====================================================================

The interface of character device provided Linux FireWire subsystem is described in
``include/uapi/linux/firewire-cdev.h`` [#fw_uapi]_.

Like the unit driver, the user space application can reserve the range of address in 1394 OHCI
controller to receive the event for the request subaction of asynchronous transaction to the
range of address. The application calls ``ioctl(2)`` with ``FW_CDEV_IOC_ALLOCATE`` request and
data of ``fw_cdev_allocate`` structure. When receiving the request subaction, Linux FireWire
subsystem allocates data of ``fw_cdev_event_request2`` structure in kernel space, and the user
space application execute ``read(2)`` to copy it to user space.

::

    struct fw_cdev_event_request2 {
        __u64 closure;
        __u32 type;
        __u32 tcode;
        __u64 offset;
        __u32 source_node_id;
        __u32 destination_node_id;
        __u32 card;
        __u32 generation;
        __u32 handle;
        __u32 length;
        __u32 data[];
    };

The structure has no room to deliver the time stamp at which the packet arrived for the request
subaction. It is need to use the other layout of structure with time stamp.

Besides, when initiating the request subaction, the user space application executes ``ioctl(2)``
with ``FW_CDEV_IOC_SEND_REQUEST`` and data of ``fw_cdev_send_request`` structure. When the packet
arrives for the response subaction, Linux FireWire subsystem prepares data of
``fw_cdev_event_response`` structure in kernel space, and the user space application execute
``read(2)`` to copy it to user space.

::

    struct fw_cdev_event_response {
        __u64 closure;
        __u32 type;
        __u32 rcode;
        __u32 length;
        __u32 data[];
    };

The structure has no room to put any time stamp. It is need to use the other layout of structure
with time stamp.

Proposals for kernel API
========================

For listeners of incoming asynchronous transaction, the series of patches [#fix_null_ptr]_ are posted
for the issue of pass of NULL pointer to callback for kernel space unit driver. In the series, the
offset and length of the request subaction is utilized to distinguish whether the request is to FCP
region or not. Then the life time of object for ``fw_transaction`` structure is maintained by
reference counting of ``kref`` structure so that character device handlers for both FCP/non-FCP
regions can access to data of the structure safely.

Additionally, a new kernel API, ``__fw_send_request()``, is added to send the request subaction of
asynchronous transaction.

::

    void __fw_send_request(struct fw_card *card, struct fw_transaction *t, int tcode,
                           int destination_id, int generation, int speed, unsigned long long offset,
                           void *payload, size_t length, union fw_transaction_callback callback,
                           bool with_tstamp, void *callback_data);

The union type, ``fw_transaction_callback``, wraps two types of callback function;
``fw_transaction_callback_t`` and ``fw_transaction_callback_with_tstamp_t``.

::

    typedef void (*fw_transaction_callback_with_tstamp_t)(struct fw_card *card, int rcode,
                                                  u32 request_tstamp, u32 response_tstamp,
                                                  void *data, size_t length,
                                                  void *callback_data);

    union fw_transaction_callback {
        fw_transaction_callback_t without_tstamp;
        fw_transaction_callback_with_tstamp_t with_tstamp;
    };

The latter callback is added newly. It receives additional two arguments for time stamps at which
the packet was sent for the request subaction of asynchronous transaction, and at which the packet
arrived for the response subaction of transaction.

The existent kernel API, ``fw_send_request()`` is replaced with static inline function as well
as the variation of function. The latter is to receive time stamp of the response subaction.

::

    static inline void fw_send_request(struct fw_card *card, struct fw_transaction *t, int tcode,
                                       int destination_id, int generation, int speed,
                                       unsigned long long offset, void *payload, size_t length,
                                       fw_transaction_callback_t callback, void *callback_data)
    {
        union fw_transaction_callback cb = {
            .without_tstamp = callback,
        };
        __fw_send_request(card, t, tcode, destination_id, generation, speed, offset, payload,
                          length, cb, false, callback_data);
    }
    
    static inline void fw_send_request_with_tstamp(struct fw_card *card, struct fw_transaction *t,
        int tcode, int destination_id, int generation, int speed, unsigned long long offset,
        void *payload, size_t length, fw_transaction_callback_with_tstamp_t callback,
        void *callback_data)
    {
        union fw_transaction_callback cb = {
            .with_tstamp = callback,
        };
        __fw_send_request(card, t, tcode, destination_id, generation, speed, offset, payload,
                          length, cb, true, callback_data);
    }

In kernel space, no kernel API is provided to deliver phy packet of IEEE 1394.

Proposals for interface of the character device
===============================================

For user space application, new version of ABI is defined in ``drivers/firewire/core-cdev.c``.

::

    #define FW_CDEV_VERSION_EVENT_ASYNC_TSTAMP  6

The user space application configures the version of ABI by calling ``ioctl(2)`` with
``FW_CDEV_IOC_GET_INFO`` request.

::

   /*
    * ABI version history
    * ...
    *  6  (6.3)     - added some event for subactions of asynchronous transaction with time stamp
    *                   - %FW_CDEV_EVENT_REQUEST3
    *                   - %FW_CDEV_EVENT_RESPONSE2
    *                   - %FW_CDEV_EVENT_PHY_PACKET_SENT2
    *                   - %FW_CDEV_EVENT_PHY_PACKET_RECEIVED2
    */
    ...
    struct fw_cdev_get_info {
        __u32 version;
        __u32 rom_length;
        __u64 rom;
        __u64 bus_reset;
        __u64 bus_reset_closure;
        __u32 card;
    };

The four new types of event are available in ``include/uapi/linux/firewire-cdev.h``.

::

    /* available since kernel version 6.3 */
    #define FW_CDEV_EVENT_REQUEST3               0x0a
    #define FW_CDEV_EVENT_RESPONSE2              0x0b
    #define FW_CDEV_EVENT_PHY_PACKET_SENT2       0x0c
    #define FW_CDEV_EVENT_PHY_PACKET_RECEIVED2   0x0d

The event of ``FW_CDEV_EVENT_REQUEST3`` delivers data of ``fw_cdev_event_request3`` structure.

::

    struct fw_cdev_event_request3 {
        __u64 closure;
        __u32 type;
        __u32 tcode;
        __u64 offset;
        __u32 source_node_id;
        __u32 destination_node_id;
        __u32 card;
        __u32 generation;
        __u32 handle;
        __u32 length;
        __u32 tstamp;
        /*
        /*
         * Padding to keep the size of structure as multiples of 8 in various architectures since
         * 4 byte alignment is used for 8 byte of object type in System V ABI for i386 architecture.
         */
        __u32 padding;
        __u32 data[];
    };

It has ``tstamp`` field which has 32 bit storage, while it stores unsigned 16 bit integer value to
expresses the isochronous cycle at which the packet arrived for the request subaction of
asynchronous transaction.

There is an issue that the size of whole structure can be differed in SYSTEM V ABI for each
architecture. In the case, the alignment size of 64 bit object (``closure`` and ``offset``) affects
the size. The padding member is added to make the size of structure multiples of 8 bytes (= least
common multiple between 4 bytes alignment in i386 ABI and 8 bytes alignment in the other ABIs) and
avoid the issue.

The event of ``FW_CDEV_EVENT_RESPONSE2`` delivers data of ``fw_cdev_event_response2`` structure.

::

    struct fw_cdev_event_response2 {
        __u64 closure;
        __u32 type;
        __u32 rcode;
        __u32 length;
        __u32 request_tstamp;
        __u32 response_tstamp;
        /*
         * Padding to keep the size of structure as multiples of 8 in various architectures since
         * 4 byte alignment is used for 8 byte of object type in System V ABI for i386 architecture.
         */
        __u32 padding;
        __u32 data[];
    };

The ``tstamp`` field consists of two 16 bit storage. The first element is for the isochronous cycle
at which the packet was sent for the request subaction of asynchronous transaction. The second
element is for the isochronous cycle at which the packet arrived for the response subaction of
transaction.

Both events of ``FW_CDEV_EVENT_PHY_PACKET_SENT2`` and ``FW_CDEV_EVENT_PHY_PACKET_RECEIVED2``
delivers data of ``fw_cdev_event_phy_packet2``.

::

    struct fw_cdev_event_phy_packet2 {
        __u64 closure;
        __u32 type;
        __u32 rcode;
        __u32 length;
        __u32 tstamp;
        __u32 data[];
    };

For both events, the ``tstamp`` field has unsigned 16 bit integer value for isochronous cycle,
while the meaning is slight different. For ``FW_CDEV_EVENT_PHY_PACKET_RECEIVED2`` event, it is for
the isochronous cycle at which the phy packet arrived. For ``FW_CDEV_EVENT_PHY_PACKET_SENT2`` event
, the ``tstamp`` field is usually for the isochronous cycle at which the phy packet was sent,
except for the case of ping phy packet. For ping phy packet, the field has the value for round-trip
time measured by hardware at 42.195 MHz resolution.

Test program
============

ALSA project hosted some repository in github.com for user space code and libhinawa is the part of
them. A remote branch [#libhinawa_integration]_ is pushed to the repository of the library to
integrate for the above changes.

The test program is written by Python 3. The library supports GObject Introspection and
PyGObject [#pygobject]_ makes library calls dynamically.

The test is done with Tascam FireOne. The result is:

::

    == READ transaction to 0x0000fffff0000904 ==
        (responded2: length 4, sent at 7 sec 5441 cycle, complete at 7 sec 5442 cycle
      data      0x80008050
      queueing  at 31 sec 5440 cycle
      sent      at 31 sec 5441 cycle
      completed at 31 sec 5442 cycle
      returned  at 31 sec 5444 cycle

    == Lock transaction to 0x0000fffff0000904 ==
        (responded2: length 4, sent at 7 sec 5445 cycle, complete at 7 sec 5447 cycle
      data      0x81008050 <- 0x80008050
      queueing  at 31 sec 5445 cycle
      sent      at 31 sec 5445 cycle
      completed at 31 sec 5447 cycle
      returned  at 31 sec 5448 cycle
        (responded2: length 4, sent at 7 sec 5449 cycle, complete at 7 sec 5451 cycle
      data      0x80008050 <- 0x81008050
      queueing  at 31 sec 5449 cycle
      sent      at 31 sec 5449 cycle
      completed at 31 sec 5451 cycle
      returned  at 31 sec 5451 cycle

    == Write transaction to 0x0000fffff0000b00 ==
        (responded2: length 0, sent at 7 sec 5459 cycle, complete at 7 sec 5461 cycle
      data      0x01ff1800ffffffff
      queueing  at 31 sec 5458 cycle
      sent      at 31 sec 5459 cycle
      completed at 31 sec 5461 cycle
      returned  at 31 sec 5462 cycle
        (requested3: offset 0x0000fffff0000d00, length 8 at 7 sec 5461 cycle)

    == FCP transaction ==
        (requested3: offset 0x0000fffff0000d00, length 8 at 7 sec 7069 cycle)
      FCP request:
        frame     0x01ff1900ffffffff
        queueing  at 31 sec 7065 cycle
        sent      at 31 sec 7066 cycle
        completed at 31 sec 7068 cycle
      FCP response:
        frame     0x0cff19009001ffff
        arrived   at 31 sec 7069 cycle
        queueing  at 31 sec 7070 cycle

We can see a few isochronous cycles is actually past during initiation and completion of
asynchronous transaction.

The test program is:

::

    #!/usr/bin/env python3

    import gi
    gi.require_versions({'GLib': '2.0', 'Hinawa': '3.0'})
    from gi.repository import GLib, Hinawa

    from threading import Thread
    from struct import pack, unpack
    from time import sleep

    node = Hinawa.FwNode.new()
    node.open('/dev/fw1')

    ctx = GLib.MainContext.new()
    src = node.create_source()
    src.attach(ctx)

    dispatcher = GLib.MainLoop.new(ctx, False)
    th = Thread(target=lambda d: d.run(), args=(dispatcher,))
    th.start()

    req = Hinawa.FwReq.new()

    cycle_time = Hinawa.CycleTimer.new()

    def handle_response2(req: Hinawa.FwReq, rcode: Hinawa.FwRcode, frame: list[int], frame_size: int,
                         request_tstamp: int, response_tstamp: int):
        sent_tstamp = Hinawa.CycleTimer.parse_tstamp(request_tstamp, [0] * 2)
        completed_tstamp = Hinawa.CycleTimer.parse_tstamp(response_tstamp, [0] * 2)
        print('    (responded2: length {0}, sent at {1[0]} sec {1[1]} cycle, {2} at {3[0]} sec {3[1]} cycle'.format(
            frame_size, sent_tstamp, rcode.value_nick, completed_tstamp))
    req.connect('responded2', handle_response2)

    offset = 0xfffff0000904

    print(f'== READ transaction to 0x{offset:016x} ==')
    _, cycle_time = node.read_cycle_timer(0, cycle_time)
    queueing_tstamp = cycle_time.get_cycle_timer()[:2]

    frame = [0] * 4
    tstamp = [0] * 2
    _, frame, tstamp = req.transaction_with_tstamp_sync(node, Hinawa.FwTcode.READ_QUADLET_REQUEST,
                                                        offset, len(frame), frame, tstamp, 100)
    _, cycle_time = node.read_cycle_timer(0, cycle_time)

    sent_tstamp = cycle_time.compute_tstamp(tstamp[0], [0] * 2)
    completed_tstamp = cycle_time.compute_tstamp(tstamp[1], [0] * 2)
    returned_tstamp = cycle_time.get_cycle_timer()[:2]

    value = unpack('>I', frame)[0]
    print(f'  data      0x{value:08x}')
    print('  queueing  at {0[0]} sec {0[1]} cycle'.format(queueing_tstamp))
    print('  sent      at {0[0]} sec {0[1]} cycle'.format(sent_tstamp))
    print('  completed at {0[0]} sec {0[1]} cycle'.format(completed_tstamp))
    print('  returned  at {0[0]} sec {0[1]} cycle'.format(returned_tstamp))

    print('')
    print(f'== Lock transaction to 0x{offset:016x} ==')

    new = value | 0x01000000
    frame = list(pack('>I', value))
    frame.extend(pack('>I', new))
    tstamp = [0] * 2

    _, cycle_time = node.read_cycle_timer(0, cycle_time)
    queueing_tstamp = cycle_time.get_cycle_timer()[:2]

    _, frame, tstamp = req.transaction_with_tstamp_sync(node, Hinawa.FwTcode.LOCK_COMPARE_SWAP, offset,
                                                        4, frame, tstamp, 100)
    _, cycle_time = node.read_cycle_timer(0, cycle_time)

    sent_tstamp = cycle_time.compute_tstamp(tstamp[0], [0] * 2)
    completed_tstamp = cycle_time.compute_tstamp(tstamp[1], [0] * 2)
    returned_tstamp = cycle_time.get_cycle_timer()[:2]

    print(f'  data      0x{new:08x} <- 0x{value:08x}')
    print('  queueing  at {0[0]} sec {0[1]} cycle'.format(queueing_tstamp))
    print('  sent      at {0[0]} sec {0[1]} cycle'.format(sent_tstamp))
    print('  completed at {0[0]} sec {0[1]} cycle'.format(completed_tstamp))
    print('  returned  at {0[0]} sec {0[1]} cycle'.format(returned_tstamp))

    frame = list(pack('>I', new))
    frame.extend(pack('>I', value))
    tstamp = [0] * 2

    _, cycle_time = node.read_cycle_timer(0, cycle_time)
    queueing_tstamp = cycle_time.get_cycle_timer()[:2]

    _, frame, tstamp = req.transaction_with_tstamp_sync(node, Hinawa.FwTcode.LOCK_COMPARE_SWAP, offset,
                                                        4, frame, tstamp, 100)
    _, cycle_time = node.read_cycle_timer(0, cycle_time)

    sent_tstamp = cycle_time.compute_tstamp(tstamp[0], [0] * 2)
    completed_tstamp = cycle_time.compute_tstamp(tstamp[1], [0] * 2)
    returned_tstamp = cycle_time.get_cycle_timer()[:2]

    print(f'  data      0x{value:08x} <- 0x{new:08x}')
    print('  queueing  at {0[0]} sec {0[1]} cycle'.format(queueing_tstamp))
    print('  sent      at {0[0]} sec {0[1]} cycle'.format(sent_tstamp))
    print('  completed at {0[0]} sec {0[1]} cycle'.format(completed_tstamp))
    print('  returned  at {0[0]} sec {0[1]} cycle'.format(returned_tstamp))

    resp = Hinawa.FwResp.new()
    resp.reserve_within_region(node, 0xfffff0000d00, 0xfffff0001000, 0x200)

    fcp = Hinawa.FwFcp()
    fcp.bind(node)

    def handle_requested3(resp: Hinawa.FwResp, tcode: Hinawa.FwRcode, offset: int, src: int, dst: int,
                          card: int, generation: int, frame: list, length: int, tstamp: int):
        arrived_tstamp = Hinawa.CycleTimer.parse_tstamp(tstamp, [0] * 2)
        print('    (requested3: offset 0x{0:016x}, length {1} at {2[0]} sec {2[1]} cycle)'.format(
            offset, length, arrived_tstamp))
        return Hinawa.FwRcode.COMPLETE
    resp.connect('requested3', handle_requested3)

    offset = 0xfffff0000b00
    request_frame = [0x01, 0xff, 0x18, 0x00, 0xff, 0xff, 0xff, 0xff]

    print('')
    print(f'== Write transaction to 0x{offset:016x} ==')
    _, cycle_time = node.read_cycle_timer(0, cycle_time)
    queueing_tstamp = cycle_time.get_cycle_timer()[:2]

    _, frame, tstamp = req.transaction_with_tstamp_sync(node, Hinawa.FwTcode.WRITE_BLOCK_REQUEST,
                                         offset, len(request_frame), request_frame, [0] * 2, 100)
    _, cycle_time = node.read_cycle_timer(0, cycle_time)

    sent_tstamp = cycle_time.compute_tstamp(tstamp[0], [0] * 2)
    completed_tstamp = cycle_time.compute_tstamp(tstamp[1], [0] * 2)
    returned_tstamp = cycle_time.get_cycle_timer()[:2]

    print('  data      0x{0[0]:02x}{0[1]:02x}{0[2]:02x}{0[3]:02x}{0[4]:02x}{0[5]:02x}{0[6]:02x}{0[7]:02x}'.format(
        request_frame))
    print('  queueing  at {0[0]} sec {0[1]} cycle'.format(queueing_tstamp))
    print('  sent      at {0[0]} sec {0[1]} cycle'.format(sent_tstamp))
    print('  completed at {0[0]} sec {0[1]} cycle'.format(completed_tstamp))
    print('  returned  at {0[0]} sec {0[1]} cycle'.format(returned_tstamp))

    # Wait for print to FCP response.
    sleep(0.2)

    print('')
    print('== FCP transaction ==')
    request = bytes([0x01, 0xff, 0x19, 0x00, 0xff, 0xff, 0xff, 0xff])

    _, cycle_time = node.read_cycle_timer(0, cycle_time)
    queueing_tstamp = cycle_time.get_cycle_timer()[:2]

    _, response, tstamp = fcp.avc_transaction_with_tstamp(request, [0] * len(request), [0] * 3, 100)
    _, cycle_time = node.read_cycle_timer(0, cycle_time)

    sent_tstamp = cycle_time.compute_tstamp(tstamp[0], [0] * 2)
    completed_tstamp = cycle_time.compute_tstamp(tstamp[1], [0] * 2)
    arrived_tstamp = cycle_time.compute_tstamp(tstamp[2], [0] * 2)

    print('  FCP request:')
    print('    frame     0x{0[0]:02x}{0[1]:02x}{0[2]:02x}{0[3]:02x}{0[4]:02x}{0[5]:02x}{0[6]:02x}{0[7]:02x}'.format(
        request))
    print('    queueing  at {0[0]} sec {0[1]} cycle'.format(queueing_tstamp))
    print('    sent      at {0[0]} sec {0[1]} cycle'.format(sent_tstamp))
    print('    completed at {0[0]} sec {0[1]} cycle'.format(completed_tstamp))

    queueing_tstamp = cycle_time.get_cycle_timer()[:2]

    print('  FCP response:')
    print('    frame     0x{0[0]:02x}{0[1]:02x}{0[2]:02x}{0[3]:02x}{0[4]:02x}{0[5]:02x}{0[6]:02x}{0[7]:02x}'.format(
        response))
    print('    arrived   at {0[0]} sec {0[1]} cycle'.format(arrived_tstamp))
    print('    queueing  at {0[0]} sec {0[1]} cycle'.format(queueing_tstamp))

    dispatcher.quit()
    th.join()

.. [#1394ohci] https://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/ohci_11.pdf
.. [#fw_kernel] https://docs.kernel.org/driver-api/firewire.html?highlight=fw_cdev_event#firewire-core-transaction-interfaces
.. [#fw_cdev] https://docs.kernel.org/driver-api/firewire.html?highlight=fw_cdev_event#firewire-char-device-data-structures
.. [#fw_uapi] https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/firewire-cdev.h
.. [#fix_null_ptr] https://lore.kernel.org/lkml/20230120090344.296451-1-o-takashi@sakamocchi.jp/
.. [#libhinawa_integration] https://github.com/alsa-project/libhinawa/tree/topic/async-context-tstamp
.. [#pygobject] https://pygobject.readthedocs.io/en/latest/

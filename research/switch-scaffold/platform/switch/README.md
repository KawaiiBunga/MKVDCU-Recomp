# Switch platform boundary

Implement a platform service only after the target runtime defines its contract. The intended ownership is:

`generated PPC -> game imports/patches -> ReXGlue runtime -> platform services -> libnx/NVK -> .nro`.

Services awaiting proven generic implementations: ReXGlue process/thread mapping, virtual-memory reservations, synchronization semantics, filesystem path policy, SDL/event adaptation, audio backend, Vulkan/NVK presentation, and shader translation. Do not substitute no-op stubs for these services.


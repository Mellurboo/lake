#include "pb.h"
#include "darray.h"
#include "string.h"
#include "assert.h"
void pbwrite(PacketBuilder* pb, const void* buf, size_t size) {
    da_reserve(pb, ALIGN16(size));
    memcpy(pb->items + pb->len, buf, size);
    pb->len += size;
}

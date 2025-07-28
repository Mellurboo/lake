#include "protocol.h"
#include "request.h"
#include "response.h"
#include "client.h"
#include "auth.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "post_quantum_cryptography.h"
#include <string.h>
#include "db_context.h"
#include "log.h"
#include "user_map.h"
#include <arpa/inet.h> // htonl

extern DbContext* db;
extern UserMap user_map;

// FIXME: all these fucking memory leaks :(((
void authAuthenticate(Client* client, Request* header){
    // TODO: send some error here:
    if(header->packet_len != KYBER_PUBLICKEYBYTES) return;
    uint8_t* pk = calloc(KYBER_PUBLICKEYBYTES, 1);
    client_read(client, pk, KYBER_PUBLICKEYBYTES);

    uint32_t userID = ~0u;
    int e = DbContext_get_user_id_from_pub_key(db, pk, &userID);
    if(e < 0) {
        error("%d: Couldn't find user", client->fd);
        return;
    } 
    info("%d: Someone is trying to log in as %d", client->fd, userID);
    #define RAND_COUNT 16
    uint8_t* ct = calloc(KYBER_CIPHERTEXTBYTES + RAND_COUNT, 1);

    uint8_t* ss = calloc(KYBER_SSBYTES, 1);
    
    
    crypto_kem_enc(ct, ss, pk);

    AES_init_ctx(&client->aes_ctx, ss);
    free(ss);

    uint8_t* randBytes = calloc(RAND_COUNT, 1);
    randombytes(randBytes, RAND_COUNT);
    memcpy(ct + KYBER_CIPHERTEXTBYTES, randBytes, RAND_COUNT);
    AES_CTR_xcrypt_buffer(&client->aes_ctx, ct + KYBER_CIPHERTEXTBYTES, RAND_COUNT);

    Response test = {
        .packet_id = header->packet_id,
        .opcode = 0,
        .packet_len = KYBER_CIPHERTEXTBYTES + RAND_COUNT
    };
    response_hton(&test);
    client_write(client, &test, sizeof(test));
    client_write(client, ct, KYBER_CIPHERTEXTBYTES + RAND_COUNT);
    free(ct);

    Request req;
    // FIXME: REMOVE asserts
    assert(client_read(client, &req, sizeof(req)) == 1);
    request_ntoh(&req);
    assert(req.packet_len == RAND_COUNT);
    uint8_t* userRandBytes = calloc(RAND_COUNT, 1);
    assert(client_read(client, userRandBytes, RAND_COUNT));

    if(memcmp(randBytes, userRandBytes, RAND_COUNT) != 0){
        free(randBytes);
        free(userRandBytes);
        //TODO: send some error here
        return;
    }

    free(randBytes);
    free(userRandBytes);

    client->userID = userID;

    UserMapBucket* user = user_map_get_or_insert(&user_map, userID);
    // TODO: mutex this sheizung
    list_remove(&client->list);
    list_insert(&user->clients, &client->list);
    info("%d: Welcome %d!", client->fd, userID);
    Response res_header;
    res_header.packet_id = header->packet_id;
    res_header.opcode = 0;
    res_header.packet_len = sizeof(uint32_t);
    response_hton(&res_header);
    client_write(client, &res_header, sizeof(res_header));
    userID = htonl(userID);
    client_write(client, &userID, sizeof(userID));

    client->secure = true;
}
protocol_func_t authProtocolFuncs[] = {
    authAuthenticate,
};
Protocol authProtocol = PROTOCOL("auth", authProtocolFuncs);

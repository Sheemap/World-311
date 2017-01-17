#include "secure.h"

Secure_Session *Secure_Connect(Secure_PubKey peer, Secure_PubKey pub, Secure_PrivKey priv,
    Net_Addr addr, Net_Port port)
{
    Secure_Session *out = Secure_NewSession();

    if(out == NULL)
    {
        Error_Print("Unable to allocate session.\n");
        return out;
    }

    if(Secure_ConnectSocket(out, addr, port))
    {
        free(out);
        return NULL;
    }

    out->key = Secure_NewSharedKey();

    if(Secure_GenSharedKey(out->key, peer, priv))
    {
        Secure_Close(out);
        return NULL;
    }

    if(Secure_SendPublicKey(out, pub))
    {
        Secure_Close(out);
        return NULL;
    }

    return out;
}

Secure_Server Secure_StartServer(Net_Port port)
{
    Secure_Server out = Net_NewSock(NET_TCP);

    Net_StartServer(out, port, NET_TCP);

    return out;
}

Secure_Session *Secure_Accept(Secure_Server server, Secure_PubKey pub,
    Secure_PrivKey priv)
{
    Secure_Session *out = Secure_NewSession();

    Secure_PubKey peer = Secure_NewPubKey();

    Secure_AcceptSocket(out, server);

    if(Secure_RecvPublicKey(out, peer))
    {
        Secure_Close(out);
        return NULL;
    }

    out->key = Secure_NewSharedKey();

    if(Secure_GenSharedKey(out->key, peer, priv))
    {
        Secure_Close(out);
        return NULL;
    }

    Secure_FreePubKey(peer);

    return out;
}

int Secure_Send(Secure_Session *session, const uint8_t *data, size_t len)
{
    int sent;

    Secure_MsgSize size;
    uint8_t *cyphertext = (uint8_t*)malloc(len + crypto_box_MACBYTES);
    uint8_t *cyphersize = (uint8_t*)malloc(sizeof(Secure_MsgSize) + crypto_box_MACBYTES);
    Secure_Nonce nonce = Secure_NewNonce();

    Secure_GenNonce(nonce);

    if(len > SECURE_MAX_MSGSIZE)
    {
        free(cyphertext);
        free(cyphersize);
        free(nonce);
        return -1;
    }

    size.value = htonl(len);

    if(crypto_box_easy_afternm(cyphersize, size.bytes, sizeof(Secure_MsgSize),
        nonce, session->key))
    {
        free(cyphertext);
        free(cyphersize);
        free(nonce);
        Error_Print("Unable to encrypt message size.\n");
        return -1;
    }

    nonce[0] ^= 1;

    if(crypto_box_easy_afternm(cyphertext, data, len, nonce, session->key))
    {
        free(cyphertext);
        free(cyphersize);
        free(nonce);
        Error_Print("Unable to encrypt message.\n");
        return -1;
    }

    nonce[0] ^= 1;

    if(Net_Send(session->sock, nonce, crypto_box_NONCEBYTES)
        != crypto_box_NONCEBYTES)
    {
        free(cyphertext);
        free(cyphersize);
        Secure_FreeNonce(nonce);
        return -1;
    }

    if(Net_Send(session->sock, cyphersize, sizeof(Secure_MsgSize) + crypto_box_MACBYTES)
        != sizeof(Secure_MsgSize) + crypto_box_MACBYTES)
    {
        free(cyphertext);
        free(cyphersize);
        Secure_FreeNonce(nonce);
        return -1;
    }

    sent = Net_Send(session->sock, cyphertext, len + crypto_box_MACBYTES);

    free(cyphertext);
    free(cyphersize);
    Secure_FreeNonce(nonce);

    return sent;
}

size_t Secure_Recv(Secure_Session *session, uint8_t **data)
{
    size_t out;

    Secure_MsgSize size;
    uint8_t *cyphertext;
    uint8_t *cyphersize = (uint8_t*)malloc(sizeof(Secure_MsgSize) + crypto_box_MACBYTES);
    Secure_Nonce nonce = Secure_NewNonce();

    if(Net_Recv(session->sock, nonce, crypto_box_NONCEBYTES)
        != crypto_box_NONCEBYTES)
    {
        free(cyphersize);
        Secure_FreeNonce(nonce);
        return -1;
    }

    if(Net_Recv(session->sock, cyphersize, sizeof(Secure_MsgSize) + crypto_box_MACBYTES)
        != sizeof(Secure_MsgSize) + crypto_box_MACBYTES)
    {
        free(cyphersize);
        Secure_FreeNonce(nonce);
        return -1;
    }

    if(crypto_box_open_easy_afternm(size.bytes, cyphersize,
        sizeof(Secure_MsgSize) + crypto_box_MACBYTES, nonce, session->key))
    {
        free(cyphersize);
        Secure_FreeNonce(nonce);
        return -1;
    }

    free(cyphersize);

    out = ntohl(size.value);

    if(out > SECURE_MAX_MSGSIZE)
    {
        Secure_FreeNonce(nonce);
        return -1;
    }

    cyphertext = (uint8_t*)malloc(out + crypto_box_MACBYTES);

    if(Net_Recv(session->sock, cyphertext, out + crypto_box_MACBYTES)
        != out + crypto_box_MACBYTES)
    {
        free(cyphertext);
        Secure_FreeNonce(nonce);
        return -1;
    }

    nonce[0] ^= 1;

    *data = (uint8_t*)malloc(out);

    if(*data == NULL)
    {
        free(cyphertext);
        Secure_FreeNonce(nonce);
        Error_Print("Unable to allocate incoming message buffer.\n");
        return -1;
    }

    if(crypto_box_open_easy_afternm(*data, cyphertext,
        out + crypto_box_MACBYTES, nonce, session->key))
    {
        free(cyphertext);
        Secure_FreeNonce(nonce);
        free(*data);
        *data = NULL;
        return -1;
    }

    free(cyphertext);
    Secure_FreeNonce(nonce);

    return out;
}

void Secure_Close(Secure_Session *session)
{
    Net_Close(session->sock);

    Secure_FreeSession(session);
}

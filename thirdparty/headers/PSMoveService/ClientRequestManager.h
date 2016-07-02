#ifndef REQUEST_MANAGER_H
#define REQUEST_MANAGER_H

//-- includes -----
#include "PSMoveProtocolInterface.h"
#include "ClientPSMoveAPI.h"

//-- definitions -----
class ClientRequestManager : public IResponseListener
{
public:
    ClientRequestManager(ClientPSMoveAPI::t_response_callback callback, void *userdata);
    virtual ~ClientRequestManager();

    void send_request(RequestPtr request);

    virtual void handle_request_canceled(RequestPtr request) override;
    virtual void handle_response(ResponsePtr response) override;

    void flush_response_cache();

private:
    // private implementation - same lifetime as the ClientRequestManager
    class ClientRequestManagerImpl *m_implementation_ptr;
};

#endif  // REQUEST_MANAGER_H

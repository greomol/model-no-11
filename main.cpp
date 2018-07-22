#include "DSSimul.h"
//#include <unistd.h>
#include <assert.h>
#include <sstream>

int workFunction_ATTN(Process *dp, Message m) {
    string s = m.getString();
    NetworkLayer *nl = dp->networkLayer;
//     printf("[%d]: received message %s from %d to %d\n", 
//       dp->node, s.c_str(), m.from, m.to);
    if (!dp->isMyMessage("ATTN", s)) return false;
    vector<int> neibs = dp->neibs();
    if (s == "*TIME") return false;
    if (s == "ATTN_INIT") {
       int arg = m.getInt();
       printf("[%d]: ATTN_INIT %d received\n", dp->node, arg);
       for (int i = 0; i < neibs.size(); i++) {
          nl->send(dp->node, neibs[i], Message("ATTN_ATTN", arg));
       }
    } else if (s == "ATTN_ATTN") {
       int arg = m.getInt();
       if (dp->context_attn.ready != arg) {
          for (int i = 0; i < neibs.size(); i++) {
             nl->send(dp->node, neibs[i], Message("ATTN_ATTN", arg));
          }
          dp->context_attn.ready = arg;
          printf("[%d]: READY=%d\n", dp->node, dp->context_attn.ready);
       }
    }
    return true;
}

int workFunction_TEST(Process *dp, Message m)
{
    string s = m.getString();
    NetworkLayer *nl = dp->networkLayer;
    if (!dp->isMyMessage("TEST", s)) return false;
    vector<int> neibs = dp->neibs(); 
    if (s == "TEST_HELLO") {
        int val = m.getInt();
        printf("TEST[%d]: HELLO %d message received from %d\n", dp->node, val, m.from);
        // Рассылаем сообщение соседям
        if (val < 2) {
            for (int i = 0; i < neibs.size(); i++) {
                nl->send(dp->node, neibs[i], Message("TEST_HELLO", val+1));
            }
        } else {
            for (int i = 0; i < neibs.size(); i++) {
                nl->send(dp->node, neibs[i], Message("TEST_BYE"));
            }
        }
    } else if (s == "TEST_BYE") {
        printf("TEST[%d]: BYE message received from %d\n", dp->node, m.from);
    }
    return true;
}

int main(int argc, char **argv)
{
    string configFile = argc > 1 ? argv[1] : "config.data";
    World w; 
    w.registerWorkFunction("TEST", workFunction_TEST);
    w.registerWorkFunction("ATTN", workFunction_ATTN);
    if (w.parseConfig(configFile)) {
        usleep(300000000);
    } else {
        printf("can't open file '%s'\n", configFile.c_str());
    }
}


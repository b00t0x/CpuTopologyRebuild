#include <IOKit/IOService.h>

class CpuTopologyRebuild : public IOService {
    OSDeclareDefaultStructors(CpuTopologyRebuild)
public:
    virtual IOService* probe(IOService* provider, SInt32* score) override;
};

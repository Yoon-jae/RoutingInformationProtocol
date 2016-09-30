#include "StdAfx.h"
#include "RIPLayer.h"
#include "RouterDlg.h"
CRIPLayer::CRIPLayer(char* pName) : CBaseLayer(pName)
{
	ResetHeader();
}

CRIPLayer::~CRIPLayer() { }

BOOL CRIPLayer::Send(int command, int dev_num, int resend)
{
	unsigned char broadcast[4];
	memset(broadcast, 0xff, 4);
	unsigned char macbroadcast[6];
	memset(macbroadcast, 0xff, 6);

	CRouterDlg * routerDlg = ((CRouterDlg *)GetUpperLayer(0));
	int messageLength;

	if (command == 1) {
		Rip_header.Rip_command = 0x01;
		CreateRequestMessage();

		// Request 인 경우에는 한개만
		messageLength = 20;
	}

	if (command == 2) {
		Rip_header.Rip_command = 0x02;
		CreateResponseMessageTable();

		// Response인 경우에는 Routing table의 Entry 개수만큼 (4Byte * 5줄)
		messageLength = CRouterDlg::route_table.GetSize() * 20;
	}

	routerDlg->m_EthernetLayer->SetDestinAddress(macbroadcast, dev_num);
	if(resend == 0 )
		routerDlg->m_IPLayer->SetDstIP(broadcast, dev_num);
	routerDlg->m_UDPLayer->SetSrcPort(0x0802); // 520(UDP)
	BOOL bSuccess = mp_UnderLayer->Send((unsigned char*)&Rip_header, RIP_HEADER_SIZE + messageLength, dev_num);
	return bSuccess;
}

BOOL CRIPLayer::Receive(unsigned char* ppayload, int dev_num)
{
	CRouterDlg::RoutingTable entry;
	CRouterDlg* routerDlg = ((CRouterDlg *) GetUpperLayer(0));
	PRipHeader pFrame = (PRipHeader) ppayload;

	// 받은 Packet에서 RIP Message에 실린 Entry의 길이(UDP 전체 길이에서 UDP header(8), RIP 맨 윗줄(4) 를 빼줌)
	unsigned short length = routerDlg->m_UDPLayer->GetLength(dev_num) - 12;

	if (pFrame->Rip_command == 0x01){// command : Request를 받은 경우, command를 Response로 변경하여 다시 보냄
		routerDlg->m_IPLayer->SetDstIP(routerDlg->m_IPLayer->GetSrcIPForRIPLayer(dev_num),dev_num);
		Send(2, dev_num , 1);
	}

	if (pFrame->Rip_command == 0x02) { // command : Response를 받은 경우, Routing table 업데이트

		// 받은 Packet에서 RIP Message에 실린 Entry의 갯수(Entry 당 길이가 20)
		int numOfEntries = length / 20;

		for (int index = 0; index < numOfEntries; index++) {
			CRouterDlg::RoutingTable entry;
			unsigned int metric = htonl(pFrame->Rip_table[index].Rip_metric);
			int selectIndex = ContainsRouteTableEntry(pFrame->Rip_table[index].Rip_ipAddress);

			if (selectIndex != -1) {	// 해당 IP가 존재한다면 비교하여 Update
				entry = CRouterDlg::route_table.GetAt(CRouterDlg::route_table.FindIndex(selectIndex));

				/*
				 * routerDlg->m_IPLayer->GetSrcIP(2) : 이 부분이 next-hop을 나타내는 것인가? 확인할 것 !!!
				 */
				if (!memcmp((unsigned char*) routerDlg->m_IPLayer->GetSrcIP(2), pFrame->Rip_table[index].Rip_nexthop, 4)) { // next-hop이 같은 경우
					memcpy(&entry.ipAddress, pFrame->Rip_table[index].Rip_ipAddress, 4);
					entry.metric = metric;
					memcpy(&entry.dstInterface, routerDlg->m_IPLayer->GetSrcIP(dev_num), 4);
					CRouterDlg::route_table.SetAt(CRouterDlg::route_table.FindIndex(selectIndex), entry);
				} else {	// next-hop이 다른 경우
					if (metric < entry.metric) {	// 새로운 metric수가 더 작으면 그걸로 Update
						memcpy(&entry.ipAddress, pFrame->Rip_table[index].Rip_ipAddress, 4);
						entry.metric = metric;
						memcpy(&entry.dstInterface, routerDlg->m_IPLayer->GetSrcIP(dev_num), 4);
						CRouterDlg::route_table.SetAt(CRouterDlg::route_table.FindIndex(selectIndex), entry);
					} else {	// 이미 존재하던 metric수가 더 작으면 그대로 둠
						;
					}
				}
			} else {	// 해당 IP가 존재하지 않으면 그대로 Routing table에 추가
				memcpy(&entry.ipAddress, pFrame->Rip_table[index].Rip_ipAddress, 4);
				entry.metric = metric;
				memcpy(&entry.dstInterface, routerDlg->m_IPLayer->GetSrcIP(dev_num), 4);
				CRouterDlg::route_table.AddTail(entry);
			}
		}
	
		routerDlg->UpdateRouteTable();
	}

	return true;
}

void CRIPLayer::ResetHeader()
{
	Rip_header.Rip_command = 0x01;    // request: 1, response: 2 
	Rip_header.Rip_version = 0x02;	  // version: 2
	Rip_header.Rip_reserved = 0x0000; // must be zero
}

void CRIPLayer::CreateRequestMessage()
{
	Rip_header.Rip_table[0].Rip_family = 0x0200;
	Rip_header.Rip_table[0].Rip_tag = 0x0100;
	memset(Rip_header.Rip_table[0].Rip_ipAddress,  0, 4);
	memset(Rip_header.Rip_table[0].Rip_subnetmask, 0, 4);
	memset(Rip_header.Rip_table[0].Rip_nexthop, 0, 4);
	Rip_header.Rip_table[0].Rip_metric = htonl(16); // Default metric : 16
}

void CRIPLayer::CreateResponseMessageTable()
{
	CRouterDlg::RoutingTable entry;
	int entries = CRouterDlg::route_table.GetCount();

	for (int index = 0; index < entries; index++) {
		entry = CRouterDlg::route_table.GetAt(CRouterDlg::route_table.FindIndex(index));

		Rip_header.Rip_table[index].Rip_family = 0x0200;
		Rip_header.Rip_table[index].Rip_tag = 0x0100;
		memcpy(Rip_header.Rip_table[index].Rip_ipAddress, entry.ipAddress, 4);
		memset(Rip_header.Rip_table[index].Rip_subnetmask, 0, 4);
		memset(Rip_header.Rip_table[index].Rip_nexthop, 0, 4);

		// Metric을 1증가 시켜줌
		Rip_header.Rip_table[index].Rip_metric = htonl(entry.metric + 1);
	}
}

// MH: Route Table에 해당 Entry가 존재하는지 확인, index를 반환한다.
int CRIPLayer::ContainsRouteTableEntry(unsigned char Ip_addr[4]) 
{
	CRouterDlg::RoutingTable entry;
	int size = CRouterDlg::route_table.GetCount();

	if (size != 0) {
		for(int index = 0; index < size; index++) {
			entry = CRouterDlg::route_table.GetAt(CRouterDlg::route_table.FindIndex(index));

			// IP가 일치하는 Entry가 존재하면 그 index return.
			if(!memcmp(Ip_addr, entry.ipAddress,  4))
				return index;
		}
	}

	return -1;
}
#include "TProtocolPokey.h"
#include <RemoteArray.h>


std::atomic<unsigned char> TProtocolPokey::mRequestCounter(0);


std::map<TPokeyCommand::Type,std::string> TPokeyCommand::EnumMap =
{
	{ TPokeyCommand::Invalid,		"Invalid" },
	{ TPokeyCommand::UnknownReply,	"UnknownReply" },
	{ TPokeyCommand::Discover,	"Discover" },
	
	{ TPokeyCommand::GetDeviceMeta,	"GetDeviceMeta" },
	{ TPokeyCommand::GetUserId,	"GetUserId" },
	{ TPokeyCommand::GetDeviceState,	"GetDeviceState" },
	
};




unsigned char TPokeyCommand::CalculateChecksum(const unsigned char * Header7)
{
	int sum = 0;
	
	for (int n = 0; n < 7; n++)
	{
		sum += Header7[n];
	}
	
	return (unsigned char)(sum % 0x100);
}


bool TProtocolPokey::DecodeGetDeviceStatus(TJob& Job,const BufferArray<unsigned char,64>& Data)
{
	//	generate string of pin states
	std::stringstream Pins;
	for (int i = 0; i < 55; i++)
	{
		bool PinState = (Data[8 + (i / 8)] & (1 << (i % 8))) > 0;
		Pins << (PinState ? '1':'0');
	}
	
	Job.mParams.AddParam("pins", Pins.str() );
	
	return true;
}


template<typename TYPE>
void TypeToHex(const TYPE Value,std::ostream& String)
{
	int TypeNibbles = 2*sizeof(Value);
	for ( int i=0;	i<TypeNibbles;	i++ )
	{
		char hex = Value >> ((TypeNibbles-1-i)*4);
		hex &= (1<<4)-1;
		if ( hex >= 10 )
			hex += 'A' - 10;
		else
			hex += '0';
		String << hex;
	}
}



bool TProtocolPokey::DecodeReply(TJob& Job,const BufferArray<unsigned char,64>& Data)
{
	//	first 8 bytes are a header
	auto RequestId = Data[6];
	auto Cmdi = Data[1];
	auto Cmd = TPokeyCommand::Validate( static_cast<TPokeyCommand::Type>(Cmdi) );

	//	check the checksum
	auto Checksum = TPokeyCommand::CalculateChecksum( Data.GetArray() );
	if ( Checksum != Data[7] )
	{
		std::Debug << "Pokey checksum failed on request " << RequestId << " command: " << Cmdi << "(" << TPokeyCommand::ToString(Cmd) << ")" << std::endl;
	}
	
	Job.mParams.mCommand = TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( Cmd );
	Job.mParams.AddParam("requestid", static_cast<int>(RequestId) );
	
	switch ( Cmd )
	{
		case TPokeyCommand::GetDeviceState:
			if ( DecodeGetDeviceStatus( Job, Data ) )
				return true;
			break;
			
		default:
			break;
	}
	
	//	unknown, put all data as hex
	std::stringstream DataString;
	for ( int i=0;	i<Data.GetSize();	i++ )
	{
		if ( i > 0 )
			DataString << " ";
		TypeToHex( Data[i], DataString );
	}
	
	Job.mParams.AddParam("data", DataString.str() );
	
	return true;
}


TDecodeResult::Type TProtocolPokey::DecodeHeader(TJob& Job,TChannelStream& Stream)
{
	//	read the first byte, if it's 0xAA we know it's a reply packet
	//	if it's not, we have to assume it's a broadcast reply with an IP...
	Array<char> Data;
	auto DataBridge = GetArrayBridge(Data);
	if ( !Stream.Pop( 1, DataBridge ) )
		return TDecodeResult::Waiting;
	
	if ( static_cast<unsigned char>(Data[0]) == 0xAA )
	{
		if ( !Stream.Pop( 64-1, DataBridge ) )
		{
			Stream.UnPop(DataBridge);
			return TDecodeResult::Waiting;
		}
		
		BufferArray<unsigned char,64> UData;
		GetArrayBridge(UData).PushBackReinterpret( Data.GetArray(), Data.GetDataSize() );
		
		if ( !DecodeReply( Job, UData ) )
			return TDecodeResult::Ignore;
		
		return TDecodeResult::Success;
	}
	else
	{
		//	old protocol size 14
		//	new protocol size 19
		//	assume is broadcast reply
		if ( !Stream.Pop( 14-1, DataBridge ) )
		{
			Stream.UnPop(DataBridge);
			return TDecodeResult::Waiting;
		}

		//	gr: not sure why but have to use some data as signed and some as unsigned... not making sense to me, maybe encoding done wrong on pokey side
		BufferArray<unsigned char, 100> UData;
		GetArrayBridge(UData).PushBackReinterpret(Data.GetArray(), Data.GetDataSize());

		std::stringstream Version;
		Version << (int)UData[3] << "." << (int)UData[4];

		//	if new protocol
		bool Protocol4913 = Version.str() == "49.13";
		bool Protocol3352 = Version.str() == "33.52";
		bool Protocol4800 = Version.str() == "48.0";

		//	same protocol as newer
		Protocol4913 |= Protocol4800;

		if ( Protocol4913 )
		{
			if ( !Stream.Pop(5, DataBridge) )
			{
				Stream.UnPop(DataBridge);
				return TDecodeResult::Waiting;
			}
			UData.Clear();
			GetArrayBridge(UData).PushBackReinterpret(Data.GetArray(), Data.GetDataSize());
		}
		else if ( Protocol3352 )
		{
			
		}
		else
		{
			std::Debug << "unknown pokey protocol " << Version.str() << std::endl;
			return TDecodeResult::Ignore;
		}
		
		int Serial = 0;
		if ( Protocol4913 )
		{
			Serial = ( (int)UData[15] << 8 ) | (int)UData[14];
		}
		else if ( Protocol3352 )
		{
			Serial = ( (int)UData[1] << 8 ) | (int)UData[2];
		}

		
		std::stringstream Address;
		Address << (int)UData[5] << "." << (int)UData[6] << "." << (int)UData[7] << "." << (int)UData[8];
		Address << ":20055";
		
		std::stringstream HostAddress;
		HostAddress << (int)UData[10] << "." << (int)UData[11] << "." << (int)UData[12] << "." << (int)UData[13];
		
		Job.mParams.mCommand = TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::Discover );
		Job.mParams.AddParam("userid", static_cast<int>(UData[0]) );
		Job.mParams.AddParam("version", Version.str() );
		Job.mParams.AddParam("serial", Serial );
		Job.mParams.AddParam("dhcpenabled", static_cast<int>(UData[9]) );
		Job.mParams.AddParam("address", Address.str() );
		Job.mParams.AddParam("hostaddress", HostAddress.str() );
		return TDecodeResult::Success;
	}
}

TDecodeResult::Type TProtocolPokey::DecodeData(TJob& Job,TChannelStream& Stream)
{
	return TDecodeResult::Success;
}

bool TProtocolPokey::Encode(const TJobReply& Reply,std::stringstream& Output)
{
	return false;
}

bool TProtocolPokey::Encode(const TJobReply& Reply,Array<char>& Output)
{
	return false;
}

bool TProtocolPokey::Encode(const TJob& Job,std::stringstream& Output)
{
	return false;
}

bool TProtocolPokey::Encode(const TJob& Job,Array<char>& Output)
{
	//	job to command id
	auto Command = TPokeyCommand::ToType( Job.mParams.mCommand );
	
	unsigned char tempOut[64-8];	//	gr: was 64, but their code NEVER uses more than 56 (64-8)
	unsigned char data2,data3,data4,data5;
	
	switch ( Command )
	{
		case TPokeyCommand::GetDeviceMeta:
		case TPokeyCommand::GetDeviceState:
			data2 = 0;
			data3 = 0;
			data4 = 0;
			data5 = 0;
			break;
			
			//	special case where we send zero bytes
		case TPokeyCommand::Discover:
			Output.PushBack(0xff);
			//Soy::Assert( Output.GetDataSize() == 0, "should send zero bytes for discovery");
			return true;
			
		default:
			return false;
	};
	
	auto RequestId = mRequestCounter++;
	unsigned char Header[8];
	
	Header[0] = 0xBB;
	Header[1] = Command;
	Header[2] = data2;
	Header[3] = data3;
	Header[4] = data4;
	Header[5] = data5;
	Header[6] = RequestId;
	Header[7] = TPokeyCommand::CalculateChecksum(Header);
	
	Output.PushBackArray( GetRemoteArray( reinterpret_cast<const char*>(Header), sizeofarray(Header) ) );
	Output.PushBackArray( GetRemoteArray( reinterpret_cast<const char*>(tempOut), sizeofarray(tempOut) ) );
	
	if ( !Soy::Assert( Output.GetDataSize()==64, "Always send 64 bytes" ) )
		return false;
	
	/*
	 
		// Wait for the response
		while(1)
		{
	 result = recv(comSocket, (char *)rxBuffer, 64, 0);
	 
	 // 64 bytes received?
	 if (result == 64)
	 {
	 if (rxBuffer[0] == 0xAA && rxBuffer[6] == RequestID)
	 {
	 if (rxBuffer[7] == CalculateChecksum(rxBuffer))
	 {
	 memcpy(Response, rxBuffer, 64);
	 return 0;
	 }
	 }
	 }
	 else if (result == 0)
	 printf("Connection closed\n");
	 else
	 printf("recv failed: %d\n", WSAGetLastError());
	 
	 
	 if (++retries1 > 10) break;
		}
		
		if (retries2++ > 3) break;
	 }
	 
	 return -1;
	 
	 
	 // Get serial number and versions
	 if (SendRequest(0x00, 0, 0, 0, 0, tempOut, tempIn) != 0) return -1;
	 deviceStat->DeviceData.SerialNumber = (int)(tempIn[2]) * 256 + (int)tempIn[3];
	 deviceStat->DeviceData.FirmwareVersionMajor = tempIn[4];
	 deviceStat->DeviceData.FirmwareVersionMinor = tempIn[5];
	 */
	return true;
}

bool TProtocolPokey::FixParamFormat(TJobParam& Param,std::stringstream& Error)
{
	return true;
}



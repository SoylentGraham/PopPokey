#include "PopPokey.h"
#include <TParameters.h>
#include <SoyDebug.h>
#include <TProtocolCli.h>
#include <TProtocolHttp.h>
#include <SoyApp.h>
#include <PopMain.h>
#include <TJobRelay.h>
#include <SoyPixels.h>
#include <SoyString.h>
#include <TFeatureBinRing.h>
#include <SortArray.h>
#include <TChannelLiteral.h>
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



	
	
TPokeyDiscoverThread::TPokeyDiscoverThread(std::shared_ptr<TChannel>& Channel) :
	mChannel		( Channel ),
	SoyWorkerThread	( Soy::GetTypeName(*this), SoyWorkerWaitMode::Sleep )
{
	Start();
}

bool TPokeyDiscoverThread::Iteration()
{
	auto Channel = mChannel;
	if ( !Channel )
		return true;
	
	//	send hello world to look for new pokeys
	TJob Job;
	Job.mParams.mCommand = TPokeyCommand::ToString( TPokeyCommand::Discover );
	Job.mChannelMeta.mChannelRef = Channel->GetChannelRef();
	Channel->SendCommand( Job );
	return true;
}




TPollPokeyThread::TPollPokeyThread(TChannelManager& Channels) :
	mChannels		( Channels ),
	SoyWorkerThread	( "TPollPokeyThread", SoyWorkerWaitMode::Sleep )
{
	Start();
}


bool TPollPokeyThread::Iteration()
{
//	SendGetDeviceMeta();
//	SendGetUserMeta();
	SendGetDeviceState();
	
	return true;
}

void TPollPokeyThread::SendGetDeviceMeta()
{
	TJob Job;
	Job.mParams.mCommand = TPokeyCommand::ToString( TPokeyCommand::GetDeviceMeta );
	SendJob( Job );
}

void TPollPokeyThread::SendGetUserMeta()
{
	TJob Job;
	Job.mParams.mCommand = TPokeyCommand::ToString( TPokeyCommand::GetUserId );
	SendJob( Job );
}

void TPollPokeyThread::SendGetDeviceState()
{
	TJob Job;
	Job.mParams.mCommand = TPokeyCommand::ToString( TPokeyCommand::GetDeviceState );
	SendJob( Job );
}

void TPollPokeyThread::SendJob(TJob& Job)
{
	for ( int i=0;	i<mPokeyChannels.GetSize();	i++ )
	{
		auto pChannel = mChannels.GetChannel( mPokeyChannels[i] );
		if ( !pChannel )
			continue;
		auto& Channel = *pChannel;
		if ( !Channel.IsConnected() )
			continue;
		
		Job.mChannelMeta.mChannelRef = Channel.GetChannelRef();
		Channel.SendCommand( Job );
	}

}


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
	auto RequestId = Data[0];
	auto Cmd = static_cast<TPokeyCommand::Type>(Data[1]);

	if ( !TPokeyCommand::IsValid(Cmd) )
		Cmd = TPokeyCommand::Invalid;

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
		//	assume is broadcast reply
		if ( !Stream.Pop( 14-1, DataBridge ) )
		{
			Stream.UnPop(DataBridge);
			return TDecodeResult::Waiting;
		}
		
		//	gr: not sure why but have to use some data as signed and some as unsigned... not making sense to me, maybe encoding done wrong on pokey side
		BufferArray<unsigned char,14> UData;
		GetArrayBridge(UData).PushBackReinterpret( Data.GetArray(), Data.GetDataSize() );
		
		int Serial = ((int)UData[1]<<8) | (int)UData[2];
		
		std::stringstream Version;
		Version << (int)UData[3] << "." << (int)UData[4];
		
		std::stringstream Address;
		Address << (int)UData[5] << "." << (int)UData[6] << "." << (int)UData[7] << "." << (int)UData[8];
		Address << ":20055";
		
		std::stringstream HostAddress;
		HostAddress << (int)UData[10] << "." << (int)UData[11] << "." << (int)UData[12] << "." << (int)UData[13];

		Job.mParams.mCommand = TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::Discover );
		Job.mParams.AddParam("userid", static_cast<int>(UData[0]) );
		Job.mParams.AddParam("version", Version.str() );
		Job.mParams.AddParam("serial", Serial );
		Job.mParams.AddParam("DhcpStatus", static_cast<int>(UData[9]) );
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






TPopPokey::TPopPokey() :
	TJobHandler	( static_cast<TChannelManager&>(*this) )
{
	TParameterTraits InitPokeyTraits;
	InitPokeyTraits.mAssumedKeys.PushBack("ref");
	InitPokeyTraits.mAssumedKeys.PushBack("address");
	InitPokeyTraits.mRequiredKeys.PushBack("ref");
	InitPokeyTraits.mRequiredKeys.PushBack("address");
	AddJobHandler("InitPokey", InitPokeyTraits, *this, &TPopPokey::OnInitPokey );

	TParameterTraits PopGridEventTraits;
	AddJobHandler("PopGridEvent", PopGridEventTraits, *this, &TPopPokey::OnPopGridEvent );

	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::UnknownReply ), TParameterTraits(), *this, &TPopPokey::OnUnknownPokeyReply );
	
	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::Discover ), TParameterTraits(), *this, &TPopPokey::OnDiscoverPokey );

	AddJobHandler( TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::GetDeviceState ), TParameterTraits(), *this, &TPopPokey::OnPokeyPollReply );
	
	mPollPokeyThread.reset( new TPollPokeyThread( static_cast<TChannelManager&>(*this) ) );
	mDiscoverPokeyThread.reset( new TPokeyDiscoverThread( mDiscoverPokeyChannel ) );
	
}

void TPopPokey::AddChannel(std::shared_ptr<TChannel> Channel)
{
	TChannelManager::AddChannel( Channel );
	if ( !Channel )
		return;
	TJobHandler::BindToChannel( *Channel );
}

TPokeyMeta TPopPokey::FindPokey(const TPokeyMeta &Pokey)
{
	for ( int i=0;	i<mPokeys.GetSize();	i++ )
	{
		auto& Match = mPokeys[i];
		if ( Pokey.mSerial == Match.mSerial )
			return Match;
		if ( Pokey.mAddress == Match.mAddress )
			return Match;
	}
	
	return TPokeyMeta();
}

TPokeyMeta TPopPokey::FindPokey(SoyRef ChannelRef)
{
	for ( int i=0;	i<mPokeys.GetSize();	i++ )
	{
		auto& Match = mPokeys[i];
		if ( Match.mChannelRef == ChannelRef )
			return Match;
	}
	
	return TPokeyMeta();
}


void TPopPokey::OnPokeyPollReply(TJobAndChannel& JobAndChannel)
{
	//	find pokey this is from, we don't have a serial or any id per-device so match channel
	auto& Job = JobAndChannel.GetJob();
	auto Pokey = FindPokey( Job.mChannelMeta.mChannelRef );
	if ( !Pokey.IsValid() )
	{
		std::Debug << "got pokey poll reply, but didn't match pokey ref " << Job.mChannelMeta.mChannelRef << std::endl;
		return;
	}
	
	//	read pins as an array of chars
	Array<char> Pins;
	if ( !Job.mParams.GetParamAs("pins", Pins ) )
	{
		std::Debug << "failed to get pokey poll pin data for " << Job.mChannelMeta.mChannelRef << std::endl;
		std::Debug << Job.mParams << std::endl;
		return;
	}
	
	//std::Debug << "pins: " << Job.mParams.GetParamAs<std::string>("pins") << std::endl;
	
	UpdatePinState( Pokey.mSerial, GetArrayBridge(Pins) );
}


void TPopPokey::OnDiscoverPokey(TJobAndChannel& JobAndChannel)
{
	//	grab it's serial and see if it already exists
	auto& Job = JobAndChannel.GetJob();
	auto RefString = Job.mParams.GetParamAs<std::string>("serial");
	SoyRef Ref( RefString.c_str() );
	
	TPokeyMeta Pokey;
	Pokey.mAddress = Job.mParams.GetParamAs<std::string>("address");
	Pokey.mSerial = Job.mParams.GetParamAs<int>("serial");
	if ( !Pokey.IsValid() )
	{
		std::Debug << "got pokey discovery with invalid params; " << Job.mParams << std::endl;
		return;
	}
	
	//	find existing pokey
	auto ExistingPokey = FindPokey( Pokey );
	
	//	same serial, different address, kill old one
	//	different serial, same address, kill old one
	//	same serial, same address, already exists
	if ( ExistingPokey.mAddress == Pokey.mAddress && ExistingPokey.mSerial == Pokey.mSerial )
	{
		//std::Debug << "Pokey #" << Pokey.mSerial << " @" << Pokey.mAddress << " already exists" << std::endl;
		return;
	}
	
	//	create a new pokey channel
	Pokey.mChannelRef = Ref;
	std::shared_ptr<TChannel> PokeyChannel( new TChan<TChannelSocketTcpClient,TProtocolPokey>( Pokey.mChannelRef, Pokey.mAddress ) );
	AddChannel( PokeyChannel );
	mPokeys.PushBack( Pokey );
	mPollPokeyThread->AddPokeyChannel( PokeyChannel->GetChannelRef() );
	
	std::Debug << "Added new Pokey #" << Pokey.mSerial << " @" << Pokey.mAddress << " - " << PokeyChannel->GetDescription() << std::endl;
}

void TPopPokey::OnInitPokey(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	auto RefString = Job.mParams.GetParamAs<std::string>("ref");
	SoyRef Ref( RefString.c_str() );
	auto Address = Job.mParams.GetParamAs<std::string>("address");

	if ( !Ref.IsValid() )
	{
		TJobReply Reply( JobAndChannel );
		std::stringstream Error;
		Error << RefString << " not a valid ref";
		Reply.mParams.AddErrorParam(Error.str());
		
		TChannel& Channel = JobAndChannel;
		Channel.OnJobCompleted( Reply );
		return;
	}

	//	create a new pokey channel
	std::shared_ptr<TChannel> PokeyChannel( new TChan<TChannelSocketTcpClient,TProtocolPokey>( Ref, Address ) );
	AddChannel( PokeyChannel );
	mPollPokeyThread->AddPokeyChannel( PokeyChannel->GetChannelRef() );
	
	TJobReply Reply( JobAndChannel );
	std::stringstream Debug;
	Debug << "Added new channel " + PokeyChannel->GetDescription();
	Reply.mParams.AddDefaultParam( Debug.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}


void TPopPokey::OnPopGridEvent(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	
	TJobReply Reply( JobAndChannel );
	std::stringstream Debug;
	Debug << "-1";
	Reply.mParams.AddDefaultParam( Debug.str() );
	
	TChannel& Channel = JobAndChannel;
	Channel.OnJobCompleted( Reply );
}


void TPopPokey::OnUnknownPokeyReply(TJobAndChannel& JobAndChannel)
{
	auto& Job = JobAndChannel.GetJob();
	TChannel& Channel = JobAndChannel;
	
	std::Debug << "got pokey reply from " << Channel.GetChannelRef() << ": RequestId #" << Job.mParams.GetParamAs<int>("requestid") << std::endl;
	std::Debug << Job.mParams << std::endl;
}

void TPopPokey::UpdatePinState(int Serial,const ArrayBridge<char>& Pins)
{
	uint64 Pin64 = 0x0;
	for ( int i=0;	i<Pins.GetSize();	i++ )
	{
		uint64 PinDown = (Pins[i]!='0') ? 1 : 0;
		Pin64 |= PinDown << i;
	}
	UpdatePinState( Serial, Pin64 );
}

void TPopPokey::UpdatePinState(int Serial,uint64 Pins)
{
	static uint64 StaticPins = 0;
	StaticPins |= Pins;
	//	update grid
	
	static int ResetCounter = 0;
	static int ResetTimeout = 100;
	if ( ResetCounter-- < 0 )
	{
		StaticPins = 0;
		ResetCounter = ResetTimeout;
	}
	
	//	reset
	if ( Serial == 0 )
		StaticPins = 0;
	
	std::Debug << "pins: ";
	for( int i=0;	i<sizeof(StaticPins)*8;	i++ )
	{
		bool Set = (StaticPins & (1<<i)) != 0;
		std::Debug << (Set?'1':'0');
	}
	
	std::Debug << std::endl;
}

//	horrible global for lambda
std::shared_ptr<TChannel> gStdioChannel;
std::shared_ptr<TChannel> gCaptureChannel;



TPopAppError::Type PopMain(TJobParams& Params)
{
	TPopPokey App;

	auto CommandLineChannel = std::shared_ptr<TChan<TChannelLiteral,TProtocolCli>>( new TChan<TChannelLiteral,TProtocolCli>( SoyRef("cmdline") ) );
	
	//	create stdio channel for commandline output
	gStdioChannel = CreateChannelFromInputString("std:", SoyRef("stdio") );

	
	App.mDiscoverPokeyChannel.reset( new TChan<TChannelSocketUdpBroadcastClient,TProtocolPokey>( SoyRef("discover"), 20055 ) );
	
	App.AddChannel( CommandLineChannel );
	App.AddChannel( App.mDiscoverPokeyChannel );
	App.AddChannel( gStdioChannel );

	
	//	bootup commands
	//std::string ConfigFilename = Params.GetParamAs<std::string>("config");
	//if ( ConfigFilename.empty() )
	//	ConfigFilename = "bootup.txt";
	
	Array<std::string> Commands;
	//Commands.PushBack("initpokey hello 10.0.0.54:20055\n");
	//ParseConfig( ConfigFilename, GetArrayBridge( Commands ) );
	for ( int i=0;	i<Commands.GetSize();	i++ )
	{
		auto Command = Commands[i];
		TProtocolCli Protocol;
		TJob Job;
		if ( !Protocol.DecodeHeader( Job, Command ) )
		{
			std::Debug << "Couldn't decode config command: " << Command << std::endl;
			continue;
		}
		CommandLineChannel->Execute( Job.mParams.mCommand, Job.mParams );
	}

	
	
	//	when the commandline SENDs a command (a reply), send it to stdout
	auto RelayFunc = [](TJobAndChannel& JobAndChannel)
	{
		if ( !gStdioChannel )
			return;
		TJob Job = JobAndChannel;
		Job.mChannelMeta.mChannelRef = gStdioChannel->GetChannelRef();
		Job.mChannelMeta.mClientRef = SoyRef();
		gStdioChannel->SendCommand( Job );
	};
	CommandLineChannel->mOnJobSent.AddListener( RelayFunc );
	
	
	
	
	//	run
	App.mConsoleApp.WaitForExit();

	gStdioChannel.reset();
	return TPopAppError::Success;
}





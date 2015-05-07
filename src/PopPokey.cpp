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



std::map<TPokeyCommand::Type,std::string> TPokeyCommand::EnumMap =
{
	{ TPokeyCommand::Invalid,		"Invalid" },
	{ TPokeyCommand::UnknownReply,	"UnknownReply" },
	
	{ TPokeyCommand::GetDeviceMeta,	"GetDeviceMeta" },
	{ TPokeyCommand::GetUserId,	"GetUserId" },
};


TPollPokeyThread::TPollPokeyThread(TChannelManager& Channels) :
	mChannels		( Channels ),
	SoyWorkerThread	( "TPollPokeyThread", SoyWorkerWaitMode::Sleep )
{
	Start();
}


bool TPollPokeyThread::Iteration()
{
	SendGetDeviceMeta();
	SendGetUserMeta();
	
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


TDecodeResult::Type TProtocolPokey::DecodeHeader(TJob& Job,TChannelStream& Stream)
{
	//	read out 64 bytes
	Array<char> Data;
	if ( !Stream.Pop( 64, GetArrayBridge(Data) ) )
		return TDecodeResult::Waiting;
	
	//	bad header... eat all bytes up to 0xAA and unpop the rest and try again
	if ( static_cast<unsigned char>(Data[0]) != 0xAA )
	{
		while ( !Data.IsEmpty() && static_cast<unsigned char>(Data[0]) != 0xAA )
		{
			Data.PopAt(0);
		}
		//	restore data (should be empty or start with AA) and try again
		Stream.UnPop( GetArrayBridge(Data) );
		return TDecodeResult::Waiting;
	}

	//	extract data
	auto RequestId = Data[6];
	/*
	deviceStat->DeviceData.SerialNumber = (int)(tempIn[2]) * 256 + (int)tempIn[3];
	deviceStat->DeviceData.FirmwareVersionMajor = tempIn[4];
	deviceStat->DeviceData.FirmwareVersionMinor = tempIn[5];
*/
	Job.mParams.mCommand = TJobParams::CommandReplyPrefix + TPokeyCommand::ToString( TPokeyCommand::UnknownReply );
	Job.mParams.AddParam("RequestId", static_cast<int>(RequestId) );
	Job.mParams.AddParam("data0", static_cast<int>(Data[0]) );
	Job.mParams.AddParam("data1", static_cast<int>(Data[1]) );
	Job.mParams.AddParam("data2", static_cast<int>(Data[2]) );
	Job.mParams.AddParam("data3", static_cast<int>(Data[3]) );
	Job.mParams.AddParam("data4", static_cast<int>(Data[4]) );
	Job.mParams.AddParam("data5", static_cast<int>(Data[5]) );
	Job.mParams.AddParam("data6", static_cast<int>(Data[6]) );
	Job.mParams.AddParam("data7", static_cast<int>(Data[7]) );
	
	return TDecodeResult::Success;
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
			data2 = 0;
			data3 = 0;
			data4 = 0;
			data5 = 0;
			break;
			
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
	
	mPollPokeyThread.reset( new TPollPokeyThread( static_cast<TChannelManager&>(*this) ) );
}

void TPopPokey::AddChannel(std::shared_ptr<TChannel> Channel)
{
	TChannelManager::AddChannel( Channel );
	if ( !Channel )
		return;
	TJobHandler::BindToChannel( *Channel );
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

	//	no reply!
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
	
	
	App.AddChannel( CommandLineChannel );
	App.AddChannel( gStdioChannel );

	
	//	bootup commands
	//std::string ConfigFilename = Params.GetParamAs<std::string>("config");
	//if ( ConfigFilename.empty() )
	//	ConfigFilename = "bootup.txt";
	
	Array<std::string> Commands;
	Commands.PushBack("initpokey hello 10.0.0.54:20055\n");
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





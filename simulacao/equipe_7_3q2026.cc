#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

int main(int argc, char* argv[])
{
    uint32_t nClientes = 1;
    bool comMobilidade = false;
    std::string protocolo = "UDP";

    CommandLine cmd;

    cmd.AddValue("nClientes", "Numero de clientes Wi-Fi", nClientes);

    cmd.AddValue("comMobilidade",
                "Ativa ou desativa a mobilidade dos clientes",
                comMobilidade);
    
    cmd.AddValue(
        "protocolo",
        "Protocolo de transporte: UDP, TCP ou MISTO",
        protocolo
    );            

    cmd.Parse(argc, argv);

    NodeContainer servidores; // 3 servidores
    servidores.Create(3);

    NodeContainer clientes;
    clientes.Create(nClientes);

    NodeContainer ap; // nó do access point
    ap.Create(1);

    NodeContainer redeCabeada;
    redeCabeada.Add(servidores);
    redeCabeada.Add(ap);

    // Configurações da rede cabeada
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));

    // Conexão ao canal

    NetDeviceContainer dispositivosCabeados;
    dispositivosCabeados = csma.Install(redeCabeada);

    // Instalação da pilha TCP/IP
    InternetStackHelper internet;
    internet.Install(redeCabeada);

    // Configuração dos endereços IPv4
    Ipv4AddressHelper endereco;
    endereco.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesCabeadas;
    interfacesCabeadas = endereco.Assign(dispositivosCabeados);

    // Configuração do Wi-Fi
    YansWifiChannelHelper canalWifi = YansWifiChannelHelper::Default();

    YansWifiPhyHelper phyWifi;
    phyWifi.SetChannel(canalWifi.Create());
    
    // Configuração do padrão Wi-Fi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);

    // Configuração do SSID da equipe
    Ssid nomeRedePoggersons = Ssid("Equipe7");

    WifiMacHelper macWifi;

    // Configuração dos clientes Wi-Fi
    macWifi.SetType(
        "ns3::StaWifiMac",
        "Ssid", SsidValue(nomeRedePoggersons),
        "ActiveProbing", BooleanValue(false)
    );

    // Instalação das interfaces Wi-Fi nos clientes
    NetDeviceContainer dispositivosClientes;
    dispositivosClientes = wifi.Install(phyWifi, macWifi, clientes);


    // Configuração do access point
    macWifi.SetType(
        "ns3::ApWifiMac",
        "Ssid", SsidValue(nomeRedePoggersons)
    );

    NetDeviceContainer dispositivosAp;
    dispositivosAp = wifi.Install(phyWifi, macWifi, ap);


    // Configuração da posição do Access Point
    MobilityHelper mobilidadeAp;

    Ptr<ListPositionAllocator> posicaoAp =
        CreateObject<ListPositionAllocator>();

    posicaoAp->Add(Vector(70.0, 70.0, 0.0));

    mobilidadeAp.SetPositionAllocator(posicaoAp);

    mobilidadeAp.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel"
    );

    mobilidadeAp.Install(ap);

    // Configuração da posição do cliente Wi-Fi
    MobilityHelper mobilidadeClientes;

    // Distribuição aleatória dos clientes em uma região de 140 m x 140 m
    mobilidadeClientes.SetPositionAllocator(
        "ns3::RandomRectanglePositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=140.0]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=140.0]")
    );

    if (comMobilidade)
    {
        mobilidadeClientes.SetMobilityModel(
            "ns3::RandomWalk2dMobilityModel",
            "Bounds", RectangleValue(Rectangle(0.0, 140.0, 0.0, 140.0)),
            "Speed", StringValue(
                "ns3::UniformRandomVariable[Min=1.0|Max=2.0]"
            ),
            "Distance", DoubleValue(10.0)
        );
    }
    else
    {
        // Clientes permanecem parados
        mobilidadeClientes.SetMobilityModel(
            "ns3::ConstantPositionMobilityModel"
        );
    }

    mobilidadeClientes.Install(clientes);

    // Instalação da pilha tcpip nos clientes 
    internet.Install(clientes);

    // Configuração da rede Wi-Fi
    Ipv4AddressHelper enderecowifi;

    enderecowifi.SetBase("192.168.0.0", "255.255.255.0");

    // Endereçamento da interface Wi-Fi do AP
    Ipv4InterfaceContainer interfacesAp;
    interfacesAp = enderecowifi.Assign(dispositivosAp);

    // Endereçamento das interfaces dos clientes
    Ipv4InterfaceContainer interfacesClientes;
    interfacesClientes = enderecowifi.Assign(dispositivosClientes);

    // Habilitação do encaminhamento IPv4 no AP
    Ptr<Ipv4> ipv4Ap = ap.Get(0)->GetObject<Ipv4>();

    // Interface cabeada do AP
    ipv4Ap->SetForwarding(
        interfacesCabeadas.Get(3).second, true
    );

    // Interface Wi-Fi do AP
    ipv4Ap->SetForwarding(
        interfacesAp.Get(0).second, true
    );


    Ipv4StaticRoutingHelper roteamento;

    for (uint32_t i = 0; i < clientes.GetN(); i++)
    {
        Ptr<Ipv4> ipv4Cliente =
            clientes.Get(i)->GetObject<Ipv4>();

        Ptr<Ipv4StaticRouting> rotaCliente =
            roteamento.GetStaticRouting(ipv4Cliente);

        rotaCliente->SetDefaultRoute(
            Ipv4Address("192.168.0.1"),
            interfacesClientes.Get(i).second
        );
    }

    // Configuração da rota de retorno no servidor s2
    Ptr<Ipv4> ipv4Servidor = servidores.Get(2)->GetObject<Ipv4>();

    Ptr<Ipv4StaticRouting> rotaServidor =
        roteamento.GetStaticRouting(ipv4Servidor);

    rotaServidor->AddNetworkRouteTo(
        Ipv4Address("192.168.0.0"),
        Ipv4Mask("255.255.255.0"),
        Ipv4Address("10.1.1.4"),
        interfacesCabeadas.Get(2).second
    );

    Ptr<PacketSink> receptorTcp;

    if (protocolo == "UDP"){
        // Configuração do servidor UDP
        uint16_t porta = 9; // porta que o servidor enviará mensagens 

        UdpEchoServerHelper servidorUdp(porta);

        ApplicationContainer aplicacaoServidor;
        aplicacaoServidor = servidorUdp.Install(servidores.Get(2));
        aplicacaoServidor.Start(Seconds(1.0));
        aplicacaoServidor.Stop(Seconds(10.0));

        // Configuração do cliente UDP
        UdpEchoClientHelper clienteUdp(
            interfacesCabeadas.GetAddress(2), porta
        );

        clienteUdp.SetAttribute("MaxPackets", UintegerValue(1));
        clienteUdp.SetAttribute("Interval", TimeValue(Seconds(1.0)));
        clienteUdp.SetAttribute("PacketSize", UintegerValue(1024));

        ApplicationContainer aplicacoesClientes;

        for (uint32_t i = 0; i < clientes.GetN(); i++){
            ApplicationContainer aplicacao =
                clienteUdp.Install(clientes.Get(i));

            aplicacoesClientes.Add(aplicacao);
        }

        aplicacoesClientes.Start(Seconds(2.0));
        aplicacoesClientes.Stop(Seconds(10.0));
    }

    else if (protocolo == "TCP"){
        // Porta utilizada pela aplicação TCP
        uint16_t portaTcp = 5000;

        // Endereço local em que o servidor aceitará conexões
        Address enderecoLocal(
            InetSocketAddress(Ipv4Address::GetAny(), portaTcp)
        );

        // Configuração do servidor TCP
        PacketSinkHelper servidorTcp(
            "ns3::TcpSocketFactory",
            enderecoLocal
        );

        // Instalação no servidor s2
        ApplicationContainer aplicacaoServidorTcp;
        aplicacaoServidorTcp = servidorTcp.Install(servidores.Get(2));

        aplicacaoServidorTcp.Start(Seconds(1.0));
        std::cout << "Servidor TCP instalado em s2, porta "
          << portaTcp << std::endl;
        
        Address destinoTcp(
            InetSocketAddress(interfacesCabeadas.GetAddress(2), portaTcp)
        );

        // Configuração dos clientes TCP
        OnOffHelper clienteTcp(
            "ns3::TcpSocketFactory",
            destinoTcp
        );

        // Taxa de geração de dados
        clienteTcp.SetAttribute("DataRate", DataRateValue(DataRate("512kbps")));

        // Tamanho dos pacotes gerados pela aplicação
        clienteTcp.SetAttribute("PacketSize", UintegerValue(1024));

        // Aplicação sempre ativa durante seu intervalo de execução
        clienteTcp.SetAttribute(
            "OnTime",
            StringValue("ns3::ConstantRandomVariable[Constant=1]")
        );

        clienteTcp.SetAttribute(
            "OffTime",
            StringValue("ns3::ConstantRandomVariable[Constant=0]")
        );

        // Instalação das aplicações TCP nos clientes
        ApplicationContainer aplicacoesClientesTcp;

        for (uint32_t i = 0; i < clientes.GetN(); i++)
        {
            ApplicationContainer aplicacao =
                clienteTcp.Install(clientes.Get(i));

            aplicacoesClientesTcp.Add(aplicacao);
        }

        aplicacoesClientesTcp.Start(Seconds(2.0));
        aplicacoesClientesTcp.Stop(Seconds(9.0));

        receptorTcp = DynamicCast<PacketSink>(aplicacaoServidorTcp.Get(0));
    }

    else if (protocolo == "MISTO"){
        uint32_t nUdp = nClientes / 2;
        uint32_t nTcp = nClientes - nUdp;

        std::cout << "\nCenario misto selecionado!" << std::endl;

        std::cout << "Clientes UDP: " << nUdp << std::endl;
        std::cout << "Clientes TCP: " << nTcp << std::endl;


        uint16_t portaUdp = 9;

        UdpEchoServerHelper servidorUdpMisto(portaUdp);

        ApplicationContainer aplicacaoServidorUdpMisto =
            servidorUdpMisto.Install(servidores.Get(2));

        aplicacaoServidorUdpMisto.Start(Seconds(1.0));
        aplicacaoServidorUdpMisto.Stop(Seconds(10.0));

        // Configuração dos clientes UDP
        UdpEchoClientHelper clienteUdpMisto(
            interfacesCabeadas.GetAddress(2), portaUdp
        );

        clienteUdpMisto.SetAttribute("MaxPackets", UintegerValue(1));
        clienteUdpMisto.SetAttribute("Interval", TimeValue(Seconds(1.0)));
        clienteUdpMisto.SetAttribute("PacketSize", UintegerValue(1024));

        // Instalação das aplicações UDP
        ApplicationContainer aplicacoesUdpMisto;

        for (uint32_t i = 0; i < nUdp; i++)
        {
            ApplicationContainer aplicacao =
                clienteUdpMisto.Install(clientes.Get(i));

            aplicacoesUdpMisto.Add(aplicacao);
        }

        aplicacoesUdpMisto.Start(Seconds(2.0));
        aplicacoesUdpMisto.Stop(Seconds(9.0));

        uint16_t portaTcpMisto = 5000;

        Address enderecoLocalTcpMisto(
            InetSocketAddress(Ipv4Address::GetAny(), portaTcpMisto)
        );

        PacketSinkHelper servidorTcpMisto(
            "ns3::TcpSocketFactory",
            enderecoLocalTcpMisto
        );

        ApplicationContainer aplicacaoServidorTcpMisto =
            servidorTcpMisto.Install(servidores.Get(2));

        aplicacaoServidorTcpMisto.Start(Seconds(1.0));

        // Guardar referência para consultar os bytes recebidos
        receptorTcp =
            DynamicCast<PacketSink>(aplicacaoServidorTcpMisto.Get(0));
        
        Address destinoTcpMisto(
            InetSocketAddress(interfacesCabeadas.GetAddress(2), portaTcpMisto)
        );

        OnOffHelper clienteTcpMisto(
            "ns3::TcpSocketFactory",
            destinoTcpMisto
        );

        clienteTcpMisto.SetAttribute(
            "DataRate",
            DataRateValue(DataRate("512kbps"))
        );

        clienteTcpMisto.SetAttribute(
            "PacketSize",
            UintegerValue(1024)
        );

        clienteTcpMisto.SetAttribute(
            "OnTime",
            StringValue("ns3::ConstantRandomVariable[Constant=1]")
        );

        clienteTcpMisto.SetAttribute(
            "OffTime",
            StringValue("ns3::ConstantRandomVariable[Constant=0]")
        );

        ApplicationContainer aplicacoesTcpMisto;

        for (uint32_t i = nUdp; i < nClientes; i++)
        {
            ApplicationContainer aplicacao =
                clienteTcpMisto.Install(clientes.Get(i));

            aplicacoesTcpMisto.Add(aplicacao);
        }

        aplicacoesTcpMisto.Start(Seconds(2.0));
        aplicacoesTcpMisto.Stop(Seconds(9.0));
    }

    

    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    std::cout << "\nQuantidade de clientes criados: "
          << clientes.GetN() << std::endl;
    
          std::cout << "\nPosicoes dos clientes:\n";

    for (uint32_t i = 0; i < clientes.GetN(); i++)
    {
        Ptr<MobilityModel> posicao =
            clientes.Get(i)->GetObject<MobilityModel>();

        std::cout << "Cliente c" << i
                << ": " << posicao->GetPosition()
                << std::endl;
    }

    std::cout << "\nCenario de mobilidade: "
          << (comMobilidade ? "Movel" : "Estatico")
          << std::endl;

    std::cout << "\nProtocolo selecionado: "
          << protocolo << std::endl;

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    
    if (protocolo == "TCP" || protocolo == "MISTO"){
        std::cout << "\nTotal de bytes TCP recebidos em s2: "
                << receptorTcp->GetTotalRx()
                << std::endl;
    }

    std::cout << "\nPosicoes finais dos clientes:\n";

    for (uint32_t i = 0; i < clientes.GetN(); i++)
    {
        Ptr<MobilityModel> posicao =
            clientes.Get(i)->GetObject<MobilityModel>();

        std::cout << "Cliente c" << i
                << ": " << posicao->GetPosition()
                << std::endl;
    }
    Simulator::Destroy();


    return 0;
}
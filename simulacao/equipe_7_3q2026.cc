#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

int main(int argc, char* argv[])
{
    uint32_t nClientes = 1;
    bool comMobilidade = false;
    std::string protocolo = "UDP";
    double tempoSimulacao = 60.0;
    double inicioAplicacoes = 2.0;
    double fimAplicacoes = 55.0;

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
        aplicacaoServidor.Stop(Seconds(tempoSimulacao));

        // Configuração do cliente UDP
        UdpEchoClientHelper clienteUdp(
            interfacesCabeadas.GetAddress(2), porta
        );

        clienteUdp.SetAttribute("MaxPackets", UintegerValue(100000));
        clienteUdp.SetAttribute("Interval", TimeValue(Seconds(0.016)));
        clienteUdp.SetAttribute("PacketSize", UintegerValue(1024));

        ApplicationContainer aplicacoesClientes;

        for (uint32_t i = 0; i < clientes.GetN(); i++){
            ApplicationContainer aplicacao =
                clienteUdp.Install(clientes.Get(i));

            aplicacoesClientes.Add(aplicacao);
        }

        aplicacoesClientes.Start(Seconds(inicioAplicacoes));
        aplicacoesClientes.Stop(Seconds(fimAplicacoes));
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

        aplicacoesClientesTcp.Start(Seconds(inicioAplicacoes));
        aplicacoesClientesTcp.Stop(Seconds(fimAplicacoes));

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
        aplicacaoServidorUdpMisto.Stop(Seconds(tempoSimulacao));

        // Configuração dos clientes UDP
        UdpEchoClientHelper clienteUdpMisto(
            interfacesCabeadas.GetAddress(2), portaUdp
        );

        clienteUdpMisto.SetAttribute("MaxPackets", UintegerValue(100000));
        clienteUdpMisto.SetAttribute("Interval", TimeValue(Seconds(0.016)));
        clienteUdpMisto.SetAttribute("PacketSize", UintegerValue(1024));

        // Instalação das aplicações UDP
        ApplicationContainer aplicacoesUdpMisto;

        for (uint32_t i = 0; i < nUdp; i++)
        {
            ApplicationContainer aplicacao =
                clienteUdpMisto.Install(clientes.Get(i));

            aplicacoesUdpMisto.Add(aplicacao);
        }

        aplicacoesUdpMisto.Start(Seconds(inicioAplicacoes));
        aplicacoesUdpMisto.Stop(Seconds(fimAplicacoes));

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

        aplicacoesTcpMisto.Start(Seconds(inicioAplicacoes));
        aplicacoesTcpMisto.Stop(Seconds(fimAplicacoes));
    }

    

    // LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    // LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

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


    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();
    Simulator::Stop(Seconds(tempoSimulacao));    
    Simulator::Run();
    monitor->CheckForLostPackets();

    auto estatisticas = monitor->GetFlowStats();

    std::cout << "\nQuantidade de fluxos identificados: "
            << estatisticas.size() << std::endl;

    
    Ptr<Ipv4FlowClassifier> classificador =
    DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    for (const auto& fluxo : estatisticas){

        FlowId id = fluxo.first;

        Ipv4FlowClassifier::FiveTuple tupla =
            classificador->FindFlow(id);

        
        if (tupla.destinationAddress != interfacesCabeadas.GetAddress(2))
        {
            continue;
        }

        // Obter as estatísticas do fluxo
        const auto& dados = fluxo.second;

        std::cout << "\nEstatisticas do fluxo " << id << std::endl;

        std::cout << "Pacotes enviados: "
                << dados.txPackets << std::endl;

        std::cout << "Pacotes recebidos: "
                << dados.rxPackets << std::endl;

        std::cout << "Bytes recebidos: "
                << dados.rxBytes << std::endl;

        std::cout << "Pacotes classificados como perdidos: "
                << dados.lostPackets << std::endl;

        double taxaPerda = 0.0;

        if (dados.txPackets > 0){
            taxaPerda =
                100.0 * (dados.txPackets - dados.rxPackets) / dados.txPackets;
        }

        std::cout << "Taxa de perda observada: "
                << taxaPerda << "%" << std::endl;

        double duracao = fimAplicacoes - inicioAplicacoes;

        double vazao = 0.0;

        if (duracao > 0) {
            vazao = (dados.rxBytes * 8.0) / (duracao * 1000.0);
        }

        // Calculo do atraso medio em milissegundos
        double atrasoMedio = 0.0;

        if (dados.rxPackets > 0){
            atrasoMedio =
                dados.delaySum.GetSeconds() * 1000.0 / dados.rxPackets;

            std::cout << "Atraso medio: "
                    << atrasoMedio << " ms" << std::endl;
        }
        else {
            std::cout << "Atraso medio: N/A (nenhum pacote recebido)"
                    << std::endl;
        }

        std::cout << "Vazao media: "
                << vazao << " kbps" << std::endl;

        std::cout << "\nFluxo " << id << std::endl;

        std::cout << "Origem: "
                << tupla.sourceAddress << std::endl;

        std::cout << "Destino: "
                << tupla.destinationAddress << std::endl;

        std::cout << "Protocolo: "
                << (tupla.protocol == 6 ? "TCP" :
                    tupla.protocol == 17 ? "UDP" : "Outro")
                << std::endl;
    }

    std::cout << "\nTempo simulado: "
          << Simulator::Now().GetSeconds()
          << " segundos" << std::endl;
    
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
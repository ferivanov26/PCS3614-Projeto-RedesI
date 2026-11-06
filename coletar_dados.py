#!/usr/bin/env python3
"""Executa os 36 cenários PCS3614 e exporta métricas do FlowMonitor.

Uso (WSL, a partir de qualquer diretório):
    python3 coletar_dados.py
    python3 coletar_dados.py --ns3 ~/Redes/ns-3.46 --fonte ~/Redes/PCS3614-Projeto-RedesI/simulacao/equipe_7_3q2026.cc
    python3 coletar_dados.py --somente 2,4 --protocolos UDP,MISTO

Não requer bibliotecas externas. NÃO altera o arquivo-fonte original;
copia a versão fornecida para scratch antes de rodar o ns-3.
"""

import argparse
import csv
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

QUANTIDADES = (1, 2, 4, 8, 16, 32)
PROTOCOLOS = ("UDP", "TCP", "MISTO")
MOBILIDADES = (0, 1)
NOME_CC = "equipe_7_3q2026.cc"
DESTINO_SERVIDOR = "10.1.1.3"

# Correspondem aos rótulos impressos pelo C++ atual.
CAMPOS = {
    "tx_packets": r"Pacotes enviados:\s*(\d+)",
    "rx_packets": r"Pacotes recebidos:\s*(\d+)",
    "rx_bytes": r"Bytes recebidos:\s*(\d+)",
    "lost_packets": r"Pacotes classificados como perdidos:\s*(\d+)",
    "perda_pct": r"Taxa de perda observada:\s*([\d.,]+)%",
    "vazao_kbps": r"Vazao media:\s*([\d.,]+)\s*kbps",
    "atraso_ms": r"Atraso medio:\s*(N/A|[\d.,]+)",
    "origem": r"Origem:\s*(\d+\.\d+\.\d+\.\d+)",
    "destino": r"Destino:\s*(\d+\.\d+\.\d+\.\d+)",
    "protocolo_fluxo": r"Protocolo:\s*(UDP|TCP)",
}

COLUNAS_FLUXO = [
    "n_clientes", "mobilidade", "cenario", "cliente", "ip_cliente",
    "protocolo_esperado", "protocolo_fluxo", "flow_id", "status",
    "tx_packets", "rx_packets", "rx_bytes", "lost_packets",
    "perda_pct", "vazao_kbps", "atraso_ms",
]
COLUNAS_RESUMO = [
    "n_clientes", "mobilidade", "cenario", "status", "fluxos_encontrados",
    "clientes_sem_fluxo", "tx_packets", "rx_packets", "rx_bytes",
    "perda_pct", "vazao_total_kbps", "atraso_medio_ms",
    "observacao",
]


def argumentos():
    pasta_repo = Path.home() / "Redes" / "PCS3614-Projeto-RedesI"
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ns3", type=Path, default=Path.home() / "Redes" / "ns-3.46",
                        help="Pasta contendo o executável ./ns3")
    parser.add_argument("--fonte", type=Path,
                        default=pasta_repo / "simulacao" / NOME_CC,
                        help="Arquivo C++ atualizado do projeto")
    parser.add_argument("--saida", type=Path,
                        default=pasta_repo / "resultados",
                        help="Pasta dos resultados CSV e logs")
    parser.add_argument("--somente", default=",".join(map(str, QUANTIDADES)),
                        help="Quantidades de clientes, separadas por vírgulas")
    parser.add_argument("--protocolos", default=",".join(PROTOCOLOS),
                        help="UDP,TCP,MISTO (pode selecionar um subconjunto)")
    parser.add_argument("--mobilidades", default="0,1",
                        help="0,1 (0=estático, 1=móvel)")
    parser.add_argument("--timeout", type=int, default=300,
                        help="Tempo máximo de execução real por cenário (s)")
    return parser.parse_args()


def lista_opcoes(texto, permitidos, converter):
    try:
        valores = [converter(x.strip()) for x in texto.split(",")]
    except ValueError as exc:
        raise ValueError(f"Lista inválida: {texto}") from exc
    if not valores or any(x not in permitidos for x in valores):
        raise ValueError(f"Opções inválidas: {texto}; permitidas: {permitidos}")
    return list(dict.fromkeys(valores))


def numero(texto):
    return float(texto.replace(",", "."))


def extrair_fluxos(saida):
    """Identifica blocos de fluxos independentemente da ordem das métricas."""
    padrao_bloco = re.compile(
        r"^Estatisticas do fluxo\s+(\d+)\s*$(.*?)(?=^Estatisticas do fluxo\s+\d+\s*$|^Tempo simulado:|\Z)",
        re.MULTILINE | re.DOTALL,
    )
    fluxos = []
    for correspondencia in padrao_bloco.finditer(saida):
        bloco = correspondencia.group(2)
        campos = {}
        for nome, padrao in CAMPOS.items():
            achado = re.search(padrao, bloco)
            if not achado:
                raise ValueError(f"Fluxo {correspondencia.group(1)} sem campo '{nome}'. "
                                 "Verifique se os cout do C++ foram alterados.")
            campos[nome] = achado.group(1)
        for nome in ("tx_packets", "rx_packets", "rx_bytes", "lost_packets"):
            campos[nome] = int(campos[nome])
        for nome in ("perda_pct", "vazao_kbps", "atraso_ms"):
            campos[nome] = (None if campos[nome] == "N/A" else numero(campos[nome]))
        campos["flow_id"] = int(correspondencia.group(1))
        fluxos.append(campos)
    if "Quantidade de fluxos identificados:" not in saida:
        raise ValueError("Saída sem marcador do FlowMonitor. O C++ atual foi copiado para scratch?")
    return fluxos


def protocolo_cliente(cenario, indice, n_clientes):
    if cenario != "MISTO":
        return cenario
    return "UDP" if indice < n_clientes // 2 else "TCP"


def montar_registros(fluxos, n_clientes, mobilidade, cenario):
    registros = []
    for i in range(n_clientes):
        ip = f"192.168.0.{i + 2}"
        esperado = protocolo_cliente(cenario, i, n_clientes)
        relacionados = [f for f in fluxos if f["origem"] == ip and
                        f["destino"] == DESTINO_SERVIDOR]
        if len(relacionados) > 1:
            raise ValueError(f"Cliente c{i}: múltiplos fluxos destinados a s2; "
                             "o coletor precisa ser adaptado antes de agregar.")
        if relacionados and relacionados[0]["protocolo_fluxo"] != esperado:
            raise ValueError(f"Cliente c{i}: protocolo inesperado: "
                             f"{relacionados[0]['protocolo_fluxo']} (esperado {esperado})")
        base = dict(n_clientes=n_clientes, mobilidade=mobilidade, cenario=cenario,
                    cliente=f"c{i}", ip_cliente=ip, protocolo_esperado=esperado)
        if relacionados:
            f = relacionados[0]
            base.update({k: f[k] for k in (
                "protocolo_fluxo", "flow_id", "tx_packets", "rx_packets", "rx_bytes",
                "lost_packets", "perda_pct", "vazao_kbps", "atraso_ms")})
            base["status"] = "SEM_RECEPCAO" if f["rx_packets"] == 0 else "OK"
        else:
            # Não inventar zero: um fluxo inexistente NÃO foi medido.
            base.update(status="SEM_FLUXO", protocolo_fluxo="", flow_id="",
                        tx_packets=None, rx_packets=None, rx_bytes=None,
                        lost_packets=None, perda_pct=None, vazao_kbps=None,
                        atraso_ms=None)
        registros.append(base)
    return registros


def resumo(registros, n_clientes, mobilidade, cenario):
    ausentes = sum(r["status"] == "SEM_FLUXO" for r in registros)
    presentes = [r for r in registros if r["status"] != "SEM_FLUXO"]
    base = dict(n_clientes=n_clientes, mobilidade=mobilidade, cenario=cenario,
                status="INCOMPLETO" if ausentes else "OK",
                fluxos_encontrados=len(presentes), clientes_sem_fluxo=ausentes,
                tx_packets=None, rx_packets=None, rx_bytes=None,
                perda_pct=None, vazao_total_kbps=None, atraso_medio_ms=None,
                observacao="Sem fluxos para alguns clientes; não foram presumidas perdas ou vazão zero."
                           if ausentes else "")
    if ausentes:
        return base
    enviados = sum(r["tx_packets"] for r in presentes)
    recebidos = sum(r["rx_packets"] for r in presentes)
    base["tx_packets"] = enviados
    base["rx_packets"] = recebidos
    base["rx_bytes"] = sum(r["rx_bytes"] for r in presentes)
    base["perda_pct"] = 100.0 * (enviados - recebidos) / enviados if enviados else None
    base["vazao_total_kbps"] = sum(r["vazao_kbps"] for r in presentes)
    # Média ponderada por pacotes recebidos; nunca incluir N/A como zero.
    base["atraso_medio_ms"] = (sum(r["atraso_ms"] * r["rx_packets"] for r in presentes
                                    if r["rx_packets"] > 0) / recebidos) if recebidos else None
    if recebidos == 0:
        base["observacao"] = "Nenhum pacote recebido; atraso indefinido."
    return base


def escrever_csv(caminho, colunas, linhas):
    with caminho.open("w", newline="", encoding="utf-8") as arquivo:
        escritor = csv.DictWriter(arquivo, fieldnames=colunas, delimiter=";")
        escritor.writeheader()
        escritor.writerows(linhas)


def main():
    args = argumentos()
    try:
        quantidades = lista_opcoes(args.somente, QUANTIDADES, int)
        protocolos = lista_opcoes(args.protocolos.upper(), PROTOCOLOS, str)
        mobilidades = lista_opcoes(args.mobilidades, MOBILIDADES, int)
    except ValueError as exc:
        sys.exit(str(exc))

    ns3 = args.ns3.expanduser().resolve()
    fonte = args.fonte.expanduser().resolve()
    saida = args.saida.expanduser().resolve()
    if not (ns3 / "ns3").is_file():
        sys.exit(f"Executável não encontrado: {ns3 / 'ns3'}")
    if not fonte.is_file():
        sys.exit(f"Arquivo C++ não encontrado: {fonte}")
    saida.mkdir(parents=True, exist_ok=True)
    (saida / "logs").mkdir(exist_ok=True)

    # Copia apenas UMA vez a versão atualizada, antes de todos os experimentos.
    destino_cpp = ns3 / "scratch" / NOME_CC
    if fonte != destino_cpp:
        shutil.copy2(fonte, destino_cpp)
    print(f"Código usado: {fonte}")
    print(f"Pasta ns-3: {ns3}")

    detalhados, resumidos, falhas = [], [], []
    cenarios = [(n, m, p) for n in quantidades for m in mobilidades for p in protocolos]
    for indice, (n, m, p) in enumerate(cenarios, 1):
        nome = f"n{n}_mob{m}_{p}"
        print(f"[{indice}/{len(cenarios)}] {nome}", flush=True)
        comando = ["./ns3", "run", f"scratch/equipe_7_3q2026 "
                   f"--nClientes={n} --comMobilidade={m} --protocolo={p}"]
        try:
            resultado = subprocess.run(comando, cwd=ns3, capture_output=True,
                                      text=True, timeout=args.timeout, check=False)
            log = (f"COMANDO: {' '.join(comando)}\n"
                   f"CODIGO_SAIDA: {resultado.returncode}\n\n"
                   f"STDOUT:\n{resultado.stdout}\n\nSTDERR:\n{resultado.stderr}")
            (saida / "logs" / f"{nome}.log").write_text(log, encoding="utf-8")
            if resultado.returncode != 0:
                raise RuntimeError(f"ns-3 retornou código {resultado.returncode}")
            fluxos = extrair_fluxos(resultado.stdout)
            registros = montar_registros(fluxos, n, m, p)
            detalhados.extend(registros)
            resumidos.append(resumo(registros, n, m, p))
            sem_fluxo = sum(r["status"] == "SEM_FLUXO" for r in registros)
            print(f"    OK: {len(registros) - sem_fluxo} fluxos, "
                  f"{sem_fluxo} clientes sem fluxo observado")
        except (subprocess.TimeoutExpired, RuntimeError, ValueError, OSError) as exc:
            falhas.append({"cenario": nome, "erro": str(exc)})
            print(f"    ERRO: {exc}", file=sys.stderr)

    escrever_csv(saida / "fluxos.csv", COLUNAS_FLUXO, detalhados)
    escrever_csv(saida / "resumo.csv", COLUNAS_RESUMO, resumidos)
    escrever_csv(saida / "erros.csv", ["cenario", "erro"], falhas)
    print(f"\nCenários concluídos: {len(resumidos)}/{len(cenarios)}")
    print(f"Fluxos/cliente exportados: {len(detalhados)}")
    print(f"Erros de execução: {len(falhas)}")
    print(f"Arquivos em: {saida}")
    print("  fluxos.csv  — métricas por cliente")
    print("  resumo.csv  — métricas agregadas por cenário")
    print("  erros.csv   — cenários com problemas")
    print("  logs/       — saídas completas das simulações")
    if falhas:
        sys.exit(1)


if __name__ == "__main__":
    main()
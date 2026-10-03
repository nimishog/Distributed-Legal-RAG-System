import os
import json
import kagglehub
from pathlib import Path
from typing import List, Dict, Any

DATA_DIR = Path(os.getenv("DATA_DIR", "/app/data"))
MAX_CHUNKS = int(os.getenv("MAX_INGESTION_CHUNKS", "1000"))

def fetch_legalbench() -> List[Dict[str, Any]]:
    """Download LegalBench-RAG-mini from Kaggle."""
    print("Downloading LegalBench-RAG-mini from Kaggle...")
    path = kagglehub.dataset_download("nguha/legalbench-rag-mini")
    print(f"Downloaded to: {path}")

    documents = []
    data_path = Path(path)

    for file_path in data_path.rglob("*.json"):
        with open(file_path) as f:
            data = json.load(f)
            if isinstance(data, list):
                documents.extend(data)
            elif isinstance(data, dict):
                documents.append(data)

    print(f"Loaded {len(documents)} raw documents")
    return documents[:MAX_CHUNKS]

def fetch_local_fallback() -> List[Dict[str, Any]]:
    """Fallback: create sample legal documents for local testing."""
    print("Using local fallback data...")
    samples = [
        {
            "text": "Section 337 of the Tariff Act of 1930 prohibits unfair methods of competition and unfair acts in the importation of articles into the United States. This includes patent infringement, trademark infringement, and misappropriation of trade secrets.",
            "metadata": {"source": "USC_19_1337", "jurisdiction": "federal", "topic": "trade_secrets"}
        },
        {
            "text": "Under California Civil Code Section 3426 et seq. (CUTSA), a trade secret is information that derives independent economic value from not being generally known and is subject to reasonable efforts to maintain secrecy. Misappropriation includes acquisition by improper means or disclosure without consent.",
            "metadata": {"source": "CA_Civ_3426", "jurisdiction": "california", "topic": "trade_secrets"}
        },
        {
            "text": "The Defend Trade Secrets Act of 2016 (18 U.S.C. 1836 et seq.) creates a federal civil cause of action for trade secret misappropriation. It provides for injunctive relief, damages, and in exceptional circumstances, ex parte seizure orders.",
            "metadata": {"source": "DTSA_2016", "jurisdiction": "federal", "topic": "trade_secrets"}
        },
        {
            "text": "In contract law, a breach occurs when a party fails to perform their obligations under the contract. Material breach allows the non-breaching party to terminate the contract and seek damages. Minor breach only allows damages.",
            "metadata": {"source": "contract_law_basics", "jurisdiction": "general", "topic": "contracts"}
        },
        {
            "text": "The statute of limitations for breach of written contract in California is 4 years (CCP 337). For oral contracts, it is 2 years (CCP 339). The clock starts when the breach occurs or is discovered.",
            "metadata": {"source": "CA_CCP_337_339", "jurisdiction": "california", "topic": "statute_of_limitations"}
        },
        {
            "text": "Negligence requires four elements: (1) duty of care, (2) breach of duty, (3) causation (actual and proximate), and (4) damages. The standard of care is that of a reasonable person under similar circumstances.",
            "metadata": {"source": "negligence_elements", "jurisdiction": "general", "topic": "torts"}
        },
        {
            "text": "Under New York law, the statute of limitations for legal malpractice is 3 years from the date of the malpractice (CPLR 214(6)). The continuous representation doctrine may toll the statute.",
            "metadata": {"source": "NY_CPLR_214", "jurisdiction": "new_york", "topic": "statute_of_limitations"}
        },
        {
            "text": "Attorney-client privilege protects confidential communications between attorney and client made for the purpose of obtaining legal advice. The privilege belongs to the client and can only be waived by the client.",
            "metadata": {"source": "attorney_client_privilege", "jurisdiction": "general", "topic": "evidence"}
        },
        {
            "text": "The work product doctrine (FRCP 26(b)(3)) protects materials prepared in anticipation of litigation by or for a party's representative. It provides broader protection than attorney-client privilege for mental impressions.",
            "metadata": {"source": "work_product_doctrine", "jurisdiction": "federal", "topic": "evidence"}
        },
        {
            "text": "In federal court, summary judgment is appropriate when there is no genuine dispute as to any material fact and the movant is entitled to judgment as a matter of law (FRCP 56). The court views evidence in light most favorable to non-movant.",
            "metadata": {"source": "FRCP_56", "jurisdiction": "federal", "topic": "civil_procedure"}
        }
    ]
    return samples * (MAX_CHUNKS // len(samples) + 1)[:MAX_CHUNKS]

def main():
    DATA_DIR.mkdir(parents=True, exist_ok=True)

    try:
        documents = fetch_legalbench()
    except Exception as e:
        print(f"Kaggle download failed: {e}. Using fallback.")
        documents = fetch_local_fallback()

    output_file = DATA_DIR / "raw_documents.json"
    with open(output_file, "w") as f:
        json.dump(documents, f, indent=2)

    print(f"Saved {len(documents)} documents to {output_file}")

if __name__ == "__main__":
    main()
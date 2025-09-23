function add_ssh_keys() {
  local key_string="${1}"
  mkdir -p ~/.ssh
  chmod 700 ~/.ssh
  echo -n "${key_string}" >~/.ssh/id_rsa_base64
  base64 --decode --ignore-garbage ~/.ssh/id_rsa_base64 >~/.ssh/id_rsa
  chmod 600 ~/.ssh/id_rsa
}
function add_doc_server_ssh_keys() {
  local key_string="${1}"
  local server_url="${2}"
  local server_user="${3}"
  add_ssh_keys "${key_string}"
  echo -e "Host ${server_url}\n\tStrictHostKeyChecking no\n\tUser ${server_user}\n" >>~/.ssh/config
}

function add_github_remote() {
  local key_string="${1}"
  local remote_url="${2}"
  add_ssh_keys "${key_string}"
  if ! grep -q "Host github.com" ~/.ssh/config 2>/dev/null; then
    printf "Host github.com\n\tStrictHostKeyChecking no\n" >>~/.ssh/config
  fi
  git remote remove github || true
  git remote add github "${remote_url}"
}

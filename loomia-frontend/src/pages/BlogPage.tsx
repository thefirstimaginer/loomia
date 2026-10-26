// Blog: notícias oficiais da própria plataforma Loomia.

import { useEffect, useState } from 'react'
import { api, type BlogPost } from '../lib/api'
import { Link } from '../lib/router'

export function BlogPage() {
  const [posts, setPosts] = useState<BlogPost[]>([])
  const [loading, setLoading] = useState(true)
  const [open, setOpen] = useState<string | null>(null)

  useEffect(() => {
    document.title = 'Blog · Loomia'
    api
      .blogPosts()
      .then((result) => setPosts(result.posts))
      .catch(() => setPosts([]))
      .finally(() => setLoading(false))
  }, [])

  return (
    <main className="page">
      <section className="page-head">
        <div>
          <p className="eyebrow">notícias da plataforma</p>
          <h1>Blog da Loomia</h1>
          <p className="lead">Novidades, atualizações e avisos oficiais direto da equipe.</p>
        </div>
      </section>

      {loading ? (
        <p className="muted">Carregando notícias…</p>
      ) : posts.length === 0 ? (
        <div className="empty-state">
          <h3>Nenhuma notícia ainda</h3>
          <p className="muted">A equipe publica aqui as novidades da plataforma. Volte em breve!</p>
          <Link to="/explore" className="ghost-button">
            Explorar mundos enquanto isso
          </Link>
        </div>
      ) : (
        <ul className="blog-list">
          {posts.map((post) => (
            <li key={post.id} className="card">
              <header className="blog-head">
                <span className="pill">{post.tag}</span>
                <small className="muted">
                  {new Date(post.createdAt).toLocaleDateString('pt-BR')} · por {post.author}
                </small>
              </header>
              <h2>{post.title}</h2>
              <p className="lead">{post.summary}</p>
              {open === post.id ? (
                <>
                  <p className="blog-body">{post.body}</p>
                  <button className="subtle-link" onClick={() => setOpen(null)}>
                    mostrar menos
                  </button>
                </>
              ) : (
                <button className="subtle-link" onClick={() => setOpen(post.id)}>
                  ler mais →
                </button>
              )}
            </li>
          ))}
        </ul>
      )}
    </main>
  )
}
